#include "core/wbfs_part.hpp"
#include <doctest/doctest.h>

#include "core/library_merge.hpp"
#include "support/mem_fs.hpp"
#include "support/wbfs_part_image.hpp"

using namespace nl;
using namespace nl::test;

namespace {

// Drei Slots, Slot 1 frei; Slot 2 belegt die Blöcke 7, 8.
std::vector<WbfsSlotSpec> threeSlots() {
    return {WbfsSlotSpec{"RNLA01", "Alpha", {3, 4}, false}, WbfsSlotSpec{}, WbfsSlotSpec{"RNLC01", "Gamma", {7, 8}, false}};
}

WbfsPartHead headOf(SparseDisk& d, u32 partLba) {
    auto prefix = readPrefix(d, partLba, d.sectorSize());
    auto h = parseWbfsPartHead(prefix);
    REQUIRE(h.isOk());
    return h.value();
}

}  // namespace

TEST_CASE("wbfs_part: Kopf, belegte Slots und Disc-Info-Offset") {
    SparseDisk d;
    writeWbfsPartition(d, 0, threeSlots());
    const WbfsPartHead h = headOf(d, 0);
    CHECK(h.nHdSec == 100u * 4096);
    CHECK(h.hdSecShift == 9);
    CHECK(h.wbfsSecShift == 21);
    CHECK(usedSlots(h) == std::vector<u32>{0, 2});
    // disc_info_lba = ALIGN_LBA(0x100 + 2 × 4482) / 512 = 19; Slot 2 ab hd-Sektor 1 + 2 × 19.
    CHECK(slotInfoOffset(h, 2) == (1 + 2 * 19) * 512u);
    // max_disc = min(512 − 12, (freeblks_lba − 1) / 19) mit freeblks_lba = (2 MiB − 100 / 8) >> 9 = 4095.
    CHECK(h.discTable.size() == 4094u / 19);
}

TEST_CASE("wbfs_part: kaputter Partitionskopf") {
    SparseDisk d;
    writeWbfsPartition(d, 0, threeSlots());
    auto bytes = readPrefix(d, 0, 512);
    bytes[0] = 'X';
    CHECK_FALSE(parseWbfsPartHead(bytes).isOk());
    auto zero = readPrefix(d, 0, 512);
    zero[4] = zero[5] = zero[6] = zero[7] = 0;  // n_hd_sec = 0
    CHECK_FALSE(parseWbfsPartHead(zero).isOk());
    auto bigHd = readPrefix(d, 0, 512);
    bigHd[8] = 13;  // 8-KiB-Sektoren schreibt kein Werkzeug
    CHECK_FALSE(parseWbfsPartHead(bigHd).isOk());
    auto smallBlock = readPrefix(d, 0, 512);
    smallBlock[9] = 14;
    CHECK_FALSE(parseWbfsPartHead(smallBlock).isOk());
}

TEST_CASE("wbfs_part: parseWbfsHead(head, 2) liefert die wlba von Slot 2") {
    SparseDisk d;
    writeWbfsPartition(d, 0, threeSlots());
    const WbfsPartHead h = headOf(d, 0);
    auto prefix = readPrefix(d, 0, static_cast<u32>(slotInfoOffset(h, 2)) + 19 * 512);
    auto l = parseWbfsHead(prefix, 2);
    REQUIRE(l.isOk());
    CHECK(l.value().wbfsSecSize == 2u * 1024 * 1024);
    CHECK(l.value().wlba[0] == 7);
    CHECK(l.value().wlba[1] == 8);
    CHECK(l.value().wlba[2] == 0);
    auto slot0 = parseWbfsHead(prefix);  // bisheriger Aufruf = Slot 0
    REQUIRE(slot0.isOk());
    CHECK(slot0.value().wlba[0] == 3);
}

TEST_CASE("wbfs_part: Slot-Index ab der Slotzahl ist WbfsInvalid") {
    SparseDisk d;
    writeWbfsPartition(d, 0, threeSlots());
    const WbfsPartHead h = headOf(d, 0);
    const u32 count = static_cast<u32>(h.discTable.size());
    auto prefix = readPrefix(d, 0, 512);
    auto r = parseWbfsHead(prefix, count);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error() == FragError::WbfsInvalid);
    auto f = wbfsSlotFrags(d, 0, count);
    REQUIRE_FALSE(f.isOk());
    CHECK(f.error() == FragError::WbfsInvalid);
    auto freeSlot = wbfsSlotFrags(d, 0, 1);  // Slot 1 ist frei
    REQUIRE_FALSE(freeSlot.isOk());
    CHECK(freeSlot.error() == FragError::WbfsInvalid);
}

TEST_CASE("wbfs_part: wlba hinter dem Partitionsende ist kaputt") {
    SparseDisk d;
    writeWbfsPartition(d, 0, {WbfsSlotSpec{"RNLA01", "Alpha", {3, 100}, false}});  // Block 100 gibt es nicht
    auto f = wbfsSlotFrags(d, 0, 0);
    REQUIRE_FALSE(f.isOk());
    CHECK(f.error() == FragError::WbfsInvalid);
}

TEST_CASE("wbfs_part: Scanner überspringt kaputten Disc-Kopf in Slot 0, Slot 2 erscheint") {
    SparseDisk d;
    auto slots = threeSlots();
    slots[0].brokenHeader = true;
    writeWbfsPartition(d, 64, slots);
    auto r = scanWbfsPartition(d, 64, nullptr);
    REQUIRE(r.isOk());
    const WbfsScan& s = r.value();
    CHECK(s.skippedSlots == std::vector<u32>{0});
    REQUIRE(s.games.size() == 1);
    CHECK(s.games[0].key == "RNLC01");
    CHECK(s.games[0].title == "Gamma");
    CHECK(s.games[0].path == "wbfs:2");
    CHECK(s.games[0].console == Console::Wii);
    CHECK(s.games[0].size == 2u * 2 * 1024 * 1024);  // zwei belegte Blöcke
}

TEST_CASE("wbfs_part: Partition ohne WBFS-Kopf liefert einen Fehler") {
    SparseDisk d;
    d.sector(0);
    CHECK_FALSE(scanWbfsPartition(d, 0, nullptr).isOk());
}

TEST_CASE("wbfs_part: Fragmentliste für Slot 2 mit partLba 2048") {
    SparseDisk d;
    writeWbfsPartition(d, 2048, threeSlots());
    auto f = wbfsSlotFrags(d, 2048, 2);
    REQUIRE(f.isOk());
    REQUIRE(f.value().entries.size() == 1);  // Blöcke 7, 8 liegen nebeneinander
    CHECK(f.value().entries[0].offset == 0);
    CHECK(f.value().entries[0].sector == 2048 + 7u * 4096);
    CHECK(f.value().entries[0].count == 2u * 4096);
}

TEST_CASE("wbfs_part: 4-KiB-Platte rechnet in 512-Byte-Sektoren (Review Focus 5)") {
    SparseDisk d(4096);
    WbfsPartSpec p;
    p.hdShift = 12;
    writeWbfsPartition(d, 256, {WbfsSlotSpec{"RNLA01", "Alpha", {5}, false}}, p);
    auto f = wbfsSlotFrags(d, 256, 0);
    REQUIRE(f.isOk());
    REQUIRE(f.value().entries.size() == 1);
    CHECK(f.value().entries[0].sector == 256u * 8 + 5u * 4096);
    CHECK(f.value().entries[0].count == 4096u);
    auto s = scanWbfsPartition(d, 256, nullptr);
    REQUIRE(s.isOk());
    REQUIRE(s.value().games.size() == 1);
    CHECK(s.value().games[0].key == "RNLA01");
}

TEST_CASE("wbfs_part: wbfsSlotOf liest nur wbfs:<dezimal>") {
    CHECK(wbfsSlotOf("wbfs:0") == std::optional<u32>{0});
    CHECK(wbfsSlotOf("wbfs:12") == std::optional<u32>{12});
    CHECK_FALSE(wbfsSlotOf("wbfs:").has_value());
    CHECK_FALSE(wbfsSlotOf("wbfs:1a").has_value());
    CHECK_FALSE(wbfsSlotOf("wbfs/Spiel [RNLA01]/RNLA01.wbfs").has_value());
}

TEST_CASE("scanDevices: WBFS-Gerät über Rohquelle, gleiche ID6 auf usb → alsoOn (Review Focus 4)") {
    test::MemFileSystem fs;
    fs.addFile("usb:/wbfs/Gamma [RNLC01]/RNLC01.iso", wiiIso("RNLC01", "Gamma"));
    SparseDisk d;
    auto slots = threeSlots();
    slots[0].brokenHeader = true;
    writeWbfsPartition(d, 0, slots);
    const std::vector<std::string> devices{"usb", "usb2"};
    const std::vector<RawSource> raw{RawSource{"usb2", &d, 0}};
    auto r = scanDevices(fs, devices, defaultScanRules(), {}, nullptr, nullptr, nullptr, raw);
    REQUIRE(r.isOk());
    REQUIRE(r.value().indexes.size() == 2);
    CHECK(r.value().indexes[1].device == "usb2");
    CHECK(r.value().stats.skipped == 1);
    CHECK(r.value().wbfsSkips.size() == 1);
    const MergedLibrary lib = mergeIndexes(r.value().indexes);
    REQUIRE(lib.games.size() == 1);
    CHECK(lib.devices[lib.games[0].device] == "usb");  // Quellenreihenfolge entscheidet
    CHECK(lib.games[0].alsoOn == std::vector<u8>{1});
}
