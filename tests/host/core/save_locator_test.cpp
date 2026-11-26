#include "core/save_locator.hpp"
#include "core/crc32.hpp"
#include <algorithm>
#include <cstdio>
#include <doctest/doctest.h>
#include "support/mem_fs.hpp"
using namespace nl;
using nl::test::MemFileSystem;

namespace {
std::vector<u8> bytes(std::string_view s) { return {s.begin(), s.end()}; }

SaveQuery wii(std::string root = "nand:") {
    SaveQuery q;
    q.kind = SaveKind::WiiDisc;
    q.id4 = "RMGP";
    q.root = std::move(root);
    return q;
}

SaveQuery rom(Console c, std::string profile, std::string stem) {
    SaveQuery q;
    q.kind = SaveKind::Rom;
    q.console = c;
    q.profile = std::move(profile);
    q.stem = std::move(stem);
    q.gameKey = "usb:/roms/x/" + q.stem;
    return q;
}

bool sameNames(const SaveUnit& u, std::vector<std::string> want) { return u.names == want; }
}  // namespace

TEST_CASE("locateSave: Wii-Spiel unter 00010000") {
    MemFileSystem fs;
    fs.addFile("nand:/title/00010000/524d4750/data/save.bin", bytes("x"));
    auto u = locateSave(fs, wii());
    CHECK(u.has_value());
    if (!u) return;
    CHECK(u->id == "wii-RMGP");
    CHECK(u->store == SaveStore::RealNand);
    CHECK(u->dir == "nand:/title/00010000/524d4750/data");
    CHECK(u->names.empty());
}

TEST_CASE("locateSave: Wii-Spiel nur unter 00010004") {
    MemFileSystem fs;
    fs.addFile("nand:/title/00010004/524d4750/data/save.bin", bytes("x"));
    auto u = locateSave(fs, wii());
    CHECK(u.has_value());
    if (u) CHECK(u->dir == "nand:/title/00010004/524d4750/data");
}

TEST_CASE("locateSave: Wii-Spiel ohne Ordner nimmt 00010000") {
    MemFileSystem fs;
    auto u = locateSave(fs, wii());
    CHECK(u.has_value());
    if (u) CHECK(u->dir == "nand:/title/00010000/524d4750/data");
}

TEST_CASE("locateSave: Wii-Spiel im EmuNAND") {
    MemFileSystem fs;
    auto u = locateSave(fs, wii("usb:/emunand"));
    CHECK(u.has_value());
    if (!u) return;
    CHECK(u->store == SaveStore::EmuNand);
    CHECK(u->dir == "usb:/emunand/title/00010000/524d4750/data");
    CHECK(u->id == "wii-RMGP");
}

TEST_CASE("locateSave: Kanal auf echtem NAND und im EmuNAND") {
    MemFileSystem fs;
    SaveQuery q;
    q.kind = SaveKind::Channel;
    q.titleId = 0x00010001'4A414145ull;
    q.root = "nand:";
    auto u = locateSave(fs, q);
    CHECK(u.has_value());
    if (u) {
        CHECK(u->id == "ch-000100014a414145");
        CHECK(u->dir == "nand:/title/00010001/4a414145/data");
        CHECK(u->store == SaveStore::RealNand);
    }
    q.root = "sd:/nands/main";
    u = locateSave(fs, q);
    CHECK(u.has_value());
    if (u) {
        CHECK(u->dir == "sd:/nands/main/title/00010001/4a414145/data");
        CHECK(u->store == SaveStore::EmuNand);
    }
}

TEST_CASE("locateSave: GameCube je Spiel und Multikarte") {
    MemFileSystem fs;
    SaveQuery q;
    q.kind = SaveKind::GcSingle;
    q.id4 = "GALE";
    q.root = "usb:";
    auto u = locateSave(fs, q);
    CHECK(u.has_value());
    if (u) {
        CHECK(u->id == "gc-GALE");
        CHECK(u->store == SaveStore::Fat);
        CHECK(u->dir == "usb:/saves");
        CHECK(sameNames(*u, {"GALE.raw"}));
    }
    q.kind = SaveKind::GcMulti;
    u = locateSave(fs, q);
    CHECK(u.has_value());
    if (u) {
        CHECK(u->id == "gc-ninmem");
        CHECK(sameNames(*u, {"ninmem.raw"}));
    }
    q.jp = true;
    u = locateSave(fs, q);
    CHECK(u.has_value());
    if (u) {
        CHECK(u->id == "gc-ninmemj");
        CHECK(sameNames(*u, {"ninmemj.raw"}));
    }
}

TEST_CASE("locateSave: fceugx Standard auf sd:") {
    MemFileSystem fs;
    auto u = locateSave(fs, rom(Console::Nes, "fceugx", "Zelda"));
    CHECK(u.has_value());
    if (!u) return;
    CHECK(u->dir == "sd:/fceugx/saves");
    CHECK(u->store == SaveStore::Fat);
    CHECK(sameNames(*u, {"Zelda.sav"}));
}

TEST_CASE("locateSave: snes9xgx mit .rtc") {
    MemFileSystem fs;
    auto u = locateSave(fs, rom(Console::Snes, "snes9xgx", "Pokemon"));
    CHECK(u.has_value());
    if (u) CHECK(sameNames(*u, {"Pokemon.srm", "Pokemon.rtc"}));
}

TEST_CASE("locateSave: vbagx auf usb: mit vorhandenem Ordner") {
    MemFileSystem fs;
    fs.addDir("usb:/vbagx/saves");
    auto u = locateSave(fs, rom(Console::Gba, "vbagx", "Metroid"));
    CHECK(u.has_value());
    if (u) CHECK(u->dir == "usb:/vbagx/saves");
    fs.addDir("sd:/vbagx/saves");
    u = locateSave(fs, rom(Console::Gba, "vbagx", "Metroid"));
    CHECK(u.has_value());
    if (u) CHECK(u->dir == "sd:/vbagx/saves");
}

TEST_CASE("locateSave: saveFolder aus settings.xml") {
    MemFileSystem fs;
    fs.addFile("sd:/apps/snes9xgx/settings.xml",
               bytes("<?xml version=\"1.0\"?>\n<file app=\"Snes9x GX\">\n <section name=\"Paths\">\n"
                     "  <setting name=\"LoadFolder\" description=\"x\" value=\"roms\" />\n"
                     "  <setting name=\"SaveFolder\" description=\"Save Folder\" value=\"spiele/snes\" />\n"
                     " </section>\n</file>\n"));
    auto u = locateSave(fs, rom(Console::Snes, "snes9xgx", "Mario"));
    CHECK(u.has_value());
    if (u) CHECK(u->dir == "sd:/spiele/snes");
}

TEST_CASE("locateSave: save_dir aus emulators.ini gewinnt und gilt für N64") {
    MemFileSystem fs;
    SaveQuery q = rom(Console::N64, "not64", "Mario64");
    CHECK(!locateSave(fs, q).has_value());
    q.saveDir = "sd:/not64/saves/";
    q.saveExt = {".eep", ".sra"};
    auto u = locateSave(fs, q);
    CHECK(u.has_value());
    if (!u) return;
    CHECK(u->dir == "sd:/not64/saves");
    CHECK(sameNames(*u, {"Mario64.eep", "Mario64.sra"}));
    q.saveExt.clear();
    u = locateSave(fs, q);
    CHECK(u.has_value());
    if (u) CHECK(sameNames(*u, {"Mario64.sav", "Mario64.srm"}));
}

TEST_CASE("locateSave: retroarch ohne Eintrag hat keinen Ort") {
    MemFileSystem fs;
    CHECK(!locateSave(fs, rom(Console::Snes, "retroarch-snes9x2010", "Mario")).has_value());
    CHECK(!locateSave(fs, rom(Console::N64, "wii64", "Mario")).has_value());
}

TEST_CASE("saveUnitId: ROM mit Slug und CRC") {
    SaveQuery q = rom(Console::Snes, "snes9xgx", "Zelda: A Link to the Past (USA)");
    char crc[9];
    std::snprintf(crc, sizeof crc, "%08x",
                  crc32(std::span<const u8>(reinterpret_cast<const u8*>(q.gameKey.data()), q.gameKey.size())));
    CHECK(saveUnitId(q) == "snes-zelda-a-link-to-the-past-usa-" + std::string(crc));
    q.stem = "Ein sehr sehr langer Spielname mit vielen Woertern (Europe) (En,De)";
    const std::string id = saveUnitId(q);
    CHECK(id.size() > 14);
    if (id.size() <= 14) return;
    const std::string slug = id.substr(5, id.size() - 5 - 9);
    CHECK(slug.size() <= 40);
    CHECK(slug.back() != '-');
    CHECK(slug == "ein-sehr-sehr-langer-spielname-mit-viele");
}

TEST_CASE("isSaveStateName") {
    CHECK(isSaveStateName("Mario Auto.frz"));
    CHECK(isSaveStateName("Zelda.fcs"));
    CHECK(isSaveStateName("Metroid.SGM"));
    CHECK(isSaveStateName("Mario64.st3"));
    CHECK(isSaveStateName("Mario.state1"));
    CHECK(isSaveStateName("shot.png"));
    CHECK(!isSaveStateName("Zelda.sav"));
    CHECK(!isSaveStateName("Pokemon.srm"));
}

TEST_CASE("readUnit: ROM-Einheit nimmt nur ihre Namen") {
    MemFileSystem fs;
    fs.addFile("sd:/snes9xgx/saves/Mario.srm", bytes("SRAM"));
    fs.addFile("sd:/snes9xgx/saves/Mario Auto.frz", bytes("STATE"));
    fs.addFile("sd:/snes9xgx/saves/Mario.png", bytes("PNG"));
    fs.addFile("sd:/snes9xgx/saves/Other.srm", bytes("O"));
    auto u = locateSave(fs, rom(Console::Snes, "snes9xgx", "Mario"));
    CHECK(u.has_value());
    if (!u) return;
    auto files = readUnit(fs, *u);
    CHECK(files.isOk());
    if (!files.isOk()) return;
    CHECK(files.value().size() == 1);
    if (files.value().size() == 1) CHECK(files.value()[0].path == "Mario.srm");
}

TEST_CASE("readUnit: Wii-Einheit rekursiv, fehlender Ordner leer, Grenze") {
    MemFileSystem fs;
    fs.addFile("nand:/title/00010000/524d4750/data/banner.bin", bytes("B"));
    fs.addFile("nand:/title/00010000/524d4750/data/sub/a.dat", bytes("AA"));
    auto u = locateSave(fs, wii());
    CHECK(u.has_value());
    if (!u) return;
    auto files = readUnit(fs, *u);
    CHECK(files.isOk());
    if (!files.isOk()) return;
    std::vector<std::string> paths;
    for (const SaveFile& f : files.value()) paths.push_back(f.path);
    std::sort(paths.begin(), paths.end());
    CHECK(paths == std::vector<std::string>{"banner.bin", "sub/a.dat"});
    CHECK(!readUnit(fs, *u, 2).isOk());
    MemFileSystem empty;
    auto none = readUnit(empty, *u);
    CHECK(none.isOk());
    if (none.isOk()) CHECK(none.value().empty());
}

TEST_CASE("unitAcceptsPath") {
    SaveUnit gb{"gb-x-00000000", SaveStore::Fat, "sd:/vbagx/saves", {"Tetris.sav"}};
    CHECK(unitAcceptsPath(gb, "Tetris.sav"));
    CHECK(!unitAcceptsPath(gb, "boot.dol"));
    CHECK(!unitAcceptsPath(gb, "../Tetris.sav"));
    SaveUnit w{"wii-RMGP", SaveStore::RealNand, "nand:/title/00010000/524d4750/data", {}};
    CHECK(unitAcceptsPath(w, "sub/a.dat"));
    CHECK(!unitAcceptsPath(w, "../../tmp/x"));
}
