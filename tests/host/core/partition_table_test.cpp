#include "core/partition_table.hpp"
#include "support/disk_image.hpp"

#include <doctest/doctest.h>

using namespace nl;
using namespace nl::test;

namespace {
constexpr u64 kDisk = 4'000'000;  // ≈ 1,9 GiB bei 512-Byte-Sektoren
}  // namespace

TEST_CASE("partition_table: mbr_four_primary") {
    MemBlockDevice d;
    writeMbrEntry(d, 0, 0x0C, 2048, 100000);
    writeMbrEntry(d, 1, 0x07, 102048, 100000);
    writeMbrEntry(d, 2, 0x07, 202048, 100000);
    writeMbrEntry(d, 3, 0x83, 302048, 100000);
    writeFatBootAt(d, 2048);
    writeNtfsBootAt(d, 102048);
    writeExFatBootAt(d, 202048);  // Typ 0x07 wie NTFS: die Art kommt aus dem Bootsektor
    writeExtSuperblock(d, 302048);
    auto r = readPartitions(d, kDisk);
    REQUIRE(r.isOk());
    const auto& p = r.value();
    REQUIRE(p.size() == 4);
    CHECK(p[0].kind == FsKind::Fat);
    CHECK(p[1].kind == FsKind::Ntfs);
    CHECK(p[2].kind == FsKind::ExFat);
    CHECK(p[3].kind == FsKind::Ext);
    CHECK(p[0].lba == 2048);
    CHECK(p[0].sectors == 100000);
    CHECK(p[0].mbrType == 0x0C);
    CHECK_FALSE(p[3].logical);
}

TEST_CASE("partition_table: ebr_chain_three_logical") {
    MemBlockDevice d;
    writeMbrEntry(d, 0, 0x0C, 2048, 100000);
    writeFatBootAt(d, 2048);
    constexpr u32 ext = 200000;
    writeMbrEntry(d, 1, 0x0F, ext, 600000);
    writeEbr(d, ext, 2048, 100000, 200000, 200000);
    writeEbr(d, ext + 200000, 2048, 100000, 400000, 200000);
    writeEbr(d, ext + 400000, 2048, 100000, 0, 0);
    writeNtfsBootAt(d, ext + 2048);
    writeFatBootAt(d, ext + 200000 + 2048);
    writeExtSuperblock(d, ext + 400000 + 2048);
    auto r = readPartitions(d, kDisk);
    REQUIRE(r.isOk());
    const auto& p = r.value();
    REQUIRE(p.size() == 4);
    CHECK_FALSE(p[0].logical);
    CHECK(p[1].logical);
    CHECK(p[2].logical);
    CHECK(p[3].logical);
    CHECK(p[1].lba == ext + 2048);
    CHECK(p[2].lba == ext + 200000 + 2048);
    CHECK(p[3].lba == ext + 400000 + 2048);
    CHECK(p[1].kind == FsKind::Ntfs);
    CHECK(p[2].kind == FsKind::Fat);
    CHECK(p[3].kind == FsKind::Ext);
}

TEST_CASE("partition_table: ebr_loop_stops") {
    MemBlockDevice d;
    constexpr u32 ext = 2048;
    writeMbrEntry(d, 0, 0x05, ext, 600000);
    writeEbr(d, ext, 2048, 100000, 200000, 200000);
    writeEbr(d, ext + 200000, 2048, 100000, 0 + 1, 200000);  // zeigt hinter den Anfang …
    // … und dieser EBR zurück auf den zweiten: Schleife.
    writeEbr(d, ext + 1, 4096, 1000, 200000, 200000);
    writeFatBootAt(d, ext + 2048);
    writeFatBootAt(d, ext + 200000 + 2048);
    auto r = readPartitions(d, kDisk);
    REQUIRE(r.isOk());
    // Drei verschiedene EBRs, dann die Wiederholung: höchstens drei logische Partitionen, keine Endlosschleife.
    CHECK(r.value().size() == 3);

    MemBlockDevice self;
    writeMbrEntry(self, 0, 0x05, ext, 600000);
    writeEbr(self, ext, 2048, 100000, 200000, 200000);
    writeEbr(self, ext + 200000, 2048, 100000, 200000, 200000);  // zeigt auf sich selbst
    auto s = readPartitions(self, kDisk);
    REQUIRE(s.isOk());
    CHECK(s.value().size() == 2);
}

TEST_CASE("partition_table: gpt_two_entries") {
    MemBlockDevice d;
    writeGpt(d, {{2048, 2048 + 99999}, {200000, 299999}}, kDisk);
    writeFatBootAt(d, 2048);
    writeExtSuperblock(d, 200000);
    auto r = readPartitions(d, kDisk);
    REQUIRE(r.isOk());
    const auto& p = r.value();
    REQUIRE(p.size() == 2);
    CHECK(p[0].lba == 2048);
    CHECK(p[0].sectors == 100000);
    CHECK(p[0].kind == FsKind::Fat);
    CHECK(p[1].lba == 200000);
    CHECK(p[1].kind == FsKind::Ext);
    CHECK(p[0].mbrType == 0xEE);
}

TEST_CASE("partition_table: gpt_bad_crc") {
    MemBlockDevice head;
    writeGpt(head, {{2048, 99999}}, kDisk);
    head.sector(1)[16] ^= 0xFF;  // Kopf-CRC
    auto r = readPartitions(head, kDisk);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().code == ErrorCode::InvalidData);

    MemBlockDevice table;
    writeGpt(table, {{2048, 99999}}, kDisk);
    table.sector(2)[40] ^= 0x01;  // Eintrag geändert, Einträge-CRC passt nicht mehr
    auto t = readPartitions(table, kDisk);
    REQUIRE_FALSE(t.isOk());
    CHECK(t.error().code == ErrorCode::InvalidData);
}

TEST_CASE("partition_table: superfloppy") {
    MemBlockDevice d;
    writeFatBootAt(d, 0);
    auto r = readPartitions(d, kDisk);
    REQUIRE(r.isOk());
    REQUIRE(r.value().size() == 1);
    CHECK(r.value()[0].lba == 0);
    CHECK(r.value()[0].sectors == kDisk);
    CHECK(r.value()[0].kind == FsKind::Fat);

    MemBlockDevice ntfs;
    writeNtfsBootAt(ntfs, 0);
    auto n = readPartitions(ntfs, kDisk);
    REQUIRE(n.isOk());
    REQUIRE(n.value().size() == 1);
    CHECK(n.value()[0].kind == FsKind::Ntfs);
}

TEST_CASE("partition_table: wbfs_partition_detected") {
    MemBlockDevice d;
    writeMbrEntry(d, 0, 0x00 + 0x0C, 2048, 100000);  // Typbyte lügt: FAT32 LBA
    writeWbfsHead(d, 2048);
    auto r = readPartitions(d, kDisk);
    REQUIRE(r.isOk());
    REQUIRE(r.value().size() == 1);
    CHECK(r.value()[0].kind == FsKind::Wbfs);

    MemBlockDevice whole;  // WBFS über die ganze Platte
    writeWbfsHead(whole, 0);
    auto w = readPartitions(whole, kDisk);
    REQUIRE(w.isOk());
    REQUIRE(w.value().size() == 1);
    CHECK(w.value()[0].kind == FsKind::Wbfs);
    CHECK(w.value()[0].lba == 0);
}

TEST_CASE("partition_table: sector_4k") {
    MemBlockDevice d(4096);
    constexpr u64 disk = 500'000;  // ≈ 1,9 GiB bei 4-KiB-Sektoren
    writeMbrEntry(d, 0, 0x0C, 256, 10000);
    writeMbrEntry(d, 1, 0x83, 20000, 10000);
    writeFatBootAt(d, 256);
    writeExtSuperblock(d, 20000);  // Byte 1080 liegt im ersten 4-KiB-Sektor
    auto r = readPartitions(d, disk);
    REQUIRE(r.isOk());
    REQUIRE(r.value().size() == 2);
    CHECK(r.value()[0].kind == FsKind::Fat);
    CHECK(r.value()[1].kind == FsKind::Ext);

    MemBlockDevice g(4096);
    writeGpt(g, {{256, 10255}}, disk);
    writeExFatBootAt(g, 256);
    auto gr = readPartitions(g, disk);
    REQUIRE(gr.isOk());
    REQUIRE(gr.value().size() == 1);
    CHECK(gr.value()[0].kind == FsKind::ExFat);
}

TEST_CASE("partition_table: zero_length_skipped") {
    MemBlockDevice d;
    writeMbrEntry(d, 0, 0x0C, 2048, 0);
    writeMbrEntry(d, 1, 0x0C, 4096, 100000);
    writeFatBootAt(d, 4096);
    auto r = readPartitions(d, kDisk);
    REQUIRE(r.isOk());
    REQUIRE(r.value().size() == 1);
    CHECK(r.value()[0].lba == 4096);
}

TEST_CASE("partition_table: beyond_disk_skipped") {
    MemBlockDevice d;
    writeMbrEntry(d, 0, 0x0C, 2048, 100000);
    writeMbrEntry(d, 1, 0x0C, u32(kDisk - 10), 100);  // ragt über das Ende
    writeMbrEntry(d, 2, 0x0C, u32(kDisk + 10), 100);  // beginnt hinter dem Ende
    writeFatBootAt(d, 2048);
    auto r = readPartitions(d, kDisk);
    REQUIRE(r.isOk());
    REQUIRE(r.value().size() == 1);
    CHECK(r.value()[0].lba == 2048);
}

TEST_CASE("partition_table: detectFs unbekannt und MBR ohne Signatur") {
    std::vector<u8> zero(512, 0);
    CHECK(detectFs(zero) == FsKind::Unknown);
    MemBlockDevice blank;
    auto r = readPartitions(blank, kDisk);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().code == ErrorCode::InvalidData);
}

TEST_CASE("partition_table: Gerätegröße 0 heißt unbekannt, keine Grenzprüfung") {
    // libogc nennt für SD und USB keine Sektorzahl (DISC_INTERFACE hat keine Abfrage dafür).
    MemBlockDevice d;
    writeMbrEntry(d, 0, 0x0C, 2048, 100000);
    writeFatBootAt(d, 2048);
    auto r = readPartitions(d, 0);
    REQUIRE(r.isOk());
    REQUIRE(r.value().size() == 1);
    CHECK(r.value()[0].kind == FsKind::Fat);
}
