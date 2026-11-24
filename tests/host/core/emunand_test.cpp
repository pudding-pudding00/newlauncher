#include "core/emunand.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/console.hpp"
#include "core/device_names.hpp"
#include "core/index_file.hpp"
#include "core/library_store.hpp"
#include "core/partition_table.hpp"
#include "core/scanner.hpp"
#include "support/mem_fs.hpp"

using namespace nl;

namespace {

void put16be(std::vector<u8>& b, std::size_t at, u16 v) {
    b[at] = static_cast<u8>(v >> 8);
    b[at + 1] = static_cast<u8>(v);
}
void put32be(std::vector<u8>& b, std::size_t at, u32 v) {
    put16be(b, at, static_cast<u16>(v >> 16));
    put16be(b, at + 2, static_cast<u16>(v));
}
void put64be(std::vector<u8>& b, std::size_t at, u64 v) {
    put32be(b, at, static_cast<u32>(v >> 32));
    put32be(b, at + 4, static_cast<u32>(v));
}

std::vector<u8> makeTmd(u64 titleId, u32 content0Id) {
    std::vector<u8> b(0x1E4 + 36, 0);
    put32be(b, 0, 0x00010001);
    put64be(b, 0x18C, titleId);
    put16be(b, 0x1DE, 1);
    put32be(b, 0x1E4, content0Id);
    put16be(b, 0x1E8, 0);  // index = 0
    return b;
}

std::vector<u8> makeImet(std::size_t magicOffset, const char* enName, const char* deName = nullptr) {
    std::vector<u8> b(0x640, 0);
    b[magicOffset + 0] = 'I';
    b[magicOffset + 1] = 'M';
    b[magicOffset + 2] = 'E';
    b[magicOffset + 3] = 'T';
    const std::size_t namesOff = magicOffset + 0x1C;
    auto writeStr = [&](std::size_t lang, const char* s) {
        if (!s) return;
        for (std::size_t i = 0; s[i] && i < 42; ++i) {
            put16be(b, namesOff + lang * 84 + i * 2, static_cast<unsigned char>(s[i]));
        }
    };
    writeStr(static_cast<std::size_t>(BannerLang::English), enName);
    writeStr(static_cast<std::size_t>(BannerLang::German), deName);
    return b;
}

class MemDisk : public BlockDevice {
public:
    std::vector<std::array<u8, 512>> sectors;
    u32 sectorSize() const override { return 512; }
    bool read(u32 lba, u32 count, std::span<u8> out) override {
        if (lba + count > sectors.size() || out.size() < count * 512u) return false;
        for (u32 i = 0; i < count; ++i) std::memcpy(out.data() + i * 512u, sectors[lba + i].data(), 512);
        return true;
    }
};

void put32le(u8* p, u32 v) {
    p[0] = static_cast<u8>(v);
    p[1] = static_cast<u8>(v >> 8);
    p[2] = static_cast<u8>(v >> 16);
    p[3] = static_cast<u8>(v >> 24);
}

}  // namespace

TEST_CASE("parseEmuNandPath: gültige und ungültige Pfade") {
    auto ok = parseEmuNandPath("usb:/nands/emu1");
    REQUIRE(ok.isOk());
    CHECK(ok.value().device == "usb");
    CHECK(ok.value().folder == "/nands/emu1");

    auto trimmed = parseEmuNandPath("SD:/emunand/");
    REQUIRE(trimmed.isOk());
    CHECK(trimmed.value().device == "sd");
    CHECK(trimmed.value().folder == "/emunand");

    CHECK_FALSE(parseEmuNandPath("").isOk());
    CHECK_FALSE(parseEmuNandPath("emunand").isOk());
    CHECK_FALSE(parseEmuNandPath(":/emunand").isOk());
    CHECK_FALSE(parseEmuNandPath("usb:/").isOk());
    CHECK_FALSE(parseEmuNandPath("usb:/nands/../evil").isOk());
    CHECK_FALSE(parseEmuNandPath("usb:/" + std::string(64, 'a')).isOk());
}

TEST_CASE("scanEmuNand: gültige Kanäle (IMET 0x40 & 0x80), kaputte TMD, Sprach-Fallback") {
    test::MemFileSystem fs;
    const u64 tid1 = 0x000100014E4C5443ull;  // NLTC
    const u64 tid2 = 0x0001000148415858ull;  // HAXX
    const u64 tid3 = 0x000100014F504552ull;  // OPER (leerer IMET → Title-ID)
    fs.addFile("sd:/emunand/title/00010001/4e4c5443/content/title.tmd", makeTmd(tid1, 0x00000005u));
    fs.addFile("sd:/emunand/title/00010001/4e4c5443/content/00000005.app", makeImet(0x80, "Test Channel", "Testkanal"));
    fs.addFile("sd:/emunand/title/00010001/48415858/content/title.tmd", makeTmd(tid2, 0x00000000u));
    fs.addFile("sd:/emunand/title/00010001/48415858/content/00000000.app", makeImet(0x40, "Homebrew Channel"));
    fs.addFile("sd:/emunand/title/00010001/4f504552/content/title.tmd", makeTmd(tid3, 0x00000001u));
    fs.addFile("sd:/emunand/title/00010001/4f504552/content/00000001.app", std::vector<u8>(0x640, 0));
    fs.addFile("sd:/emunand/title/00010001/42414431/content/title.tmd", std::vector<u8>{1, 2, 3});  // kaputt
    fs.addFile("sd:/emunand/title/00010001/42414432/content/title.tmd", makeTmd(0x0001000142414432ull, 0));  // content0 fehlt

    const EmuNandPath p{"sd", "/emunand"};
    u32 skipped = 0;
    const auto list = scanEmuNand(fs, p, BannerLang::German, &skipped);
    CHECK(skipped == 2);
    REQUIRE(list.size() == 3);

    auto findTid = [&](u64 id) -> const EmuTitle* {
        for (const auto& t : list)
            if (t.titleId == id) return &t;
        return nullptr;
    };
    const EmuTitle* t1 = findTid(tid1);
    REQUIRE(t1 != nullptr);
    CHECK(t1->name == "Testkanal");
    CHECK(t1->content0 == 5u);
    CHECK(t1->content0Path == "sd:/emunand/title/00010001/4e4c5443/content/00000005.app");

    const EmuTitle* t2 = findTid(tid2);
    REQUIRE(t2 != nullptr);
    CHECK(t2->name == "Homebrew Channel");  // kein DE → EN-Fallback

    const EmuTitle* t3 = findTid(tid3);
    REQUIRE(t3 != nullptr);
    CHECK(t3->name == "000100014F504552");  // leerer IMET → Title-ID
}

TEST_CASE("checkEmuNand: Gerät, Dateisystem, primäre MBR-Partition und title-Ordner") {
    test::MemFileSystem fs;
    fs.addDir("sd:/emunand/title/00010001");
    fs.addDir("usb:/nand/title/00010001");
    const std::vector<DeviceSlot> devs = {
        DeviceSlot{"sd", false, 0, FsKind::Fat, true, SkipReason::None, false, 0},
        DeviceSlot{"usb", true, 2048, FsKind::Fat, true, SkipReason::None, false, 1},
        DeviceSlot{"usb2", true, 4096, FsKind::Ntfs, true, SkipReason::None, true, 2},
        DeviceSlot{"usb3", true, 8192, FsKind::Fat, true, SkipReason::None, false, -1},  // GPT/logisch
    };

    CHECK(checkEmuNand(fs, "", devs).status == EmuNandStatus::NotConfigured);
    CHECK(checkEmuNand(fs, "bad", devs).status == EmuNandStatus::InvalidPath);
    CHECK(checkEmuNand(fs, "usb4:/emu", devs).status == EmuNandStatus::DeviceMissing);
    CHECK(checkEmuNand(fs, "usb2:/emu", devs).status == EmuNandStatus::NotFat);
    CHECK(checkEmuNand(fs, "usb3:/emu", devs).status == EmuNandStatus::NotPrimaryMbr);
    CHECK(checkEmuNand(fs, "sd:/missing", devs).status == EmuNandStatus::MissingTitleDir);

    const EmuNandInfo okUsb = checkEmuNand(fs, "usb:/nand", devs);
    CHECK(okUsb.status == EmuNandStatus::Ok);
    CHECK(okUsb.usb);
    CHECK(okUsb.mbrIndex == 1);
}

TEST_CASE("PartitionInfo::mbrIndex und DeviceSlot::mbrIndex") {
    MemDisk disk;
    disk.sectors.resize(4096);
    disk.sectors[0][510] = 0x55;
    disk.sectors[0][511] = 0xAA;
    // Eintrag 2 in der MBR: FAT32 LBA (0x0C) ab Sektor 2048
    u8* e2 = &disk.sectors[0][0x1BE + 16 * 2];
    e2[4] = 0x0C;
    put32le(e2 + 8, 2048);
    put32le(e2 + 12, 2048);
    disk.sectors[2048][510] = 0x55;
    disk.sectors[2048][511] = 0xAA;
    std::memcpy(&disk.sectors[2048][0x52], "FAT32   ", 8);

    auto parts = readPartitions(disk, 4096);
    REQUIRE(parts.isOk());
    REQUIRE(parts.value().size() == 1);
    CHECK(parts.value()[0].mbrIndex == 2);
    const auto slots = assignDevices(true, FsKind::Fat, parts.value(), FsSupport{});
    REQUIRE(slots.size() == 2);
    CHECK(slots[0].mbrIndex == 0);  // sd
    CHECK(slots[1].mbrIndex == 2);  // usb
}

TEST_CASE("Console::Channel: kConsoleCount = 9, v1-Index 0..7 lädt weiter, Scanner überspringt Kanäle") {
    CHECK(kConsoleCount == 9);
    CHECK(static_cast<u8>(Console::Channel) == 8);

    IndexData legacy;
    legacy.games.push_back(IndexGame{Console::Wii, 0, "RMCP01", "Mario Kart Wii", "wbfs/RMCP01.wbfs", 100});
    const auto encoded = writeIndex(legacy);
    auto decoded = readIndex(encoded);
    REQUIRE(decoded.isOk());
    REQUIRE(decoded.value().games.size() == 1);
    CHECK(decoded.value().games[0].console == Console::Wii);

    test::MemFileSystem fs;
    fs.addFile("sd:/roms/nes/a.nes", {});
    const auto sr = scanLibrary(fs, "sd:", defaultScanRules(), nullptr, nullptr, nullptr, nullptr);
    REQUIRE(sr.isOk());
    CHECK(sr.value().index.games.size() == 1);
    CHECK(sr.value().index.games[0].console == Console::Nes);

    LibraryStore store;
    store.setGames(legacy, nullptr);
    store.setChannels({EmuTitle{0x000100014E4C5443ull, "Testkanal", 5, "sd:/emunand/title/00010001/4e4c5443/content/00000005.app"}});
    const Game* ch = store.findByKey("000100014E4C5443");
    REQUIRE(ch != nullptr);
    CHECK(ch->console == Console::Channel);
    CHECK(std::string(ch->title) == "Testkanal");
    CHECK(ch->device == kNoDevice);
}
