#include "core/launch_args.hpp"
#include "core/launch_layout.hpp"
#include "core/launch_plan.hpp"
#include "support/dol_images.hpp"
#include "support/mem_fs.hpp"
#include <doctest/doctest.h>
#include <string>
using namespace nl;

namespace {
constexpr const char* kDol = "sd:/apps/fceugx/boot.dol";

std::vector<u8> goodDol() { return test::dolImage({{0x100, 0x80004000, 0x200}}, {}, 0x80004000); }

Game nesGame(const char* path = "roms/nes/Zelda (Europe) (Rev 1).nes", Console c = Console::Nes) {
    return Game{c, "nes/Zelda (Europe) (Rev 1).nes", "Zelda", 0, 0, nullptr, path, 0, 0};
}

test::MemFileSystem fsWith(std::vector<u8> dol) {
    test::MemFileSystem fs;
    fs.addFile(kDol, std::move(dol));
    fs.addFile("sd:/roms/nes/Zelda (Europe) (Rev 1).nes", {});
    return fs;
}

EmulatorTable emus() { return EmulatorTable::fromIni(IniDoc::parse("")); }
}  // namespace

TEST_CASE("gültiger Start liefert DOL-Pfad, Argumente und argv-Block") {
    const auto dol = goodDol();
    auto fs = fsWith(dol);
    auto r = preflightEmulator(fs, nesGame(), "sd:", emus());
    REQUIRE(r.isOk());
    CHECK(r.value().dolPath == kDol);
    CHECK(r.value().dolSize == dol.size());
    REQUIRE(r.value().args.size() == 3);
    CHECK(r.value().args[1] == "sd:/roms/nes/");
    CHECK(r.value().args[2] == "Zelda (Europe) (Rev 1).nes");
    CHECK_FALSE(r.value().argvBlock.empty());
}

TEST_CASE("DOL fehlt → EmuMissing mit DOL-Pfad") {
    test::MemFileSystem fs;
    fs.addFile("sd:/roms/nes/Zelda (Europe) (Rev 1).nes", {});
    auto r = preflightEmulator(fs, nesGame(), "sd:", emus());
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().kind == LaunchFailure::EmuMissing);
    CHECK(r.error().context == kDol);
}

TEST_CASE("DOL 0 Byte bzw. Müll → EmuInvalid") {
    auto empty = fsWith({});
    auto r = preflightEmulator(empty, nesGame(), "sd:", emus());
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().kind == LaunchFailure::EmuInvalid);
    CHECK(r.error().context == kDol);
    auto garbage = fsWith(std::vector<u8>(0x400, 0xAB));
    auto g = preflightEmulator(garbage, nesGame(), "sd:", emus());
    REQUIRE_FALSE(g.isOk());
    CHECK(g.error().kind == LaunchFailure::EmuInvalid);
}

TEST_CASE("Emulator-Datei ohne .dol-Endung: fehlt → EmuMissing, Müll → EmuInvalid, jeweils mit Pfad") {
    constexpr const char* kElf = "sd:/apps/snes9xgx/boot.elf";
    const EmulatorTable t = EmulatorTable::fromIni(IniDoc::parse(std::string("[nes]\ndol = ") + kElf + "\n"));
    test::MemFileSystem fs;
    fs.addFile("sd:/roms/nes/Zelda (Europe) (Rev 1).nes", {});
    auto missing = preflightEmulator(fs, nesGame(), "sd:", t);
    REQUIRE_FALSE(missing.isOk());
    CHECK(missing.error().kind == LaunchFailure::EmuMissing);
    CHECK(missing.error().context == kElf);
    fs.addFile(kElf, std::vector<u8>(0x400, 0xAB));
    auto invalid = preflightEmulator(fs, nesGame(), "sd:", t);
    REQUIRE_FALSE(invalid.isOk());
    CHECK(invalid.error().kind == LaunchFailure::EmuInvalid);
    CHECK(invalid.error().context == kElf);
}

TEST_CASE("DOL größer als kMaxDolBytes → EmuInvalid") {
    // Sektion endet weit hinter dem Header, Datei ist entsprechend groß; Größe kommt aus list().
    auto img = test::dolImage({{0x100, 0x80004000, 0x200}}, {}, 0x80004000);
    img.resize(kMaxDolBytes + 1, 0);
    auto fs = fsWith(std::move(img));
    auto r = preflightEmulator(fs, nesGame(), "sd:", emus());
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().kind == LaunchFailure::EmuInvalid);
    CHECK(r.error().context == kDol);
}

TEST_CASE("ROM fehlt → RomMissing mit ROM-Pfad") {
    test::MemFileSystem fs;
    fs.addFile(kDol, goodDol());
    auto r = preflightEmulator(fs, nesGame(), "sd:", emus());
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().kind == LaunchFailure::RomMissing);
    CHECK(r.error().context == "sd:/roms/nes/Zelda (Europe) (Rev 1).nes");
}

TEST_CASE("N64/GC/Wii → Unsupported mit Konsolenname") {
    auto fs = fsWith(goodDol());
    for (Console c : {Console::N64, Console::Gc, Console::Wii}) {
        auto r = preflightEmulator(fs, nesGame("roms/n64/x.z64", c), "sd:", emus());
        REQUIRE_FALSE(r.isOk());
        CHECK(r.error().kind == LaunchFailure::Unsupported);
        CHECK(r.error().context == consoleInfo(c).name);
    }
}

TEST_CASE("argv zu lang → ArgsTooLong") {
    auto fs = fsWith(goodDol());
    const EmulatorTable t = EmulatorTable::fromIni(IniDoc::parse("[nes]\nargs = " + std::string(kMaxArgvBytes, 'x') + "\n"));
    auto r = preflightEmulator(fs, nesGame(), "sd:", t);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().kind == LaunchFailure::ArgsTooLong);
}

TEST_CASE("launchFailureName nennt jede Fehlerart") {
    CHECK(std::string(launchFailureName(LaunchFailure::Unsupported)) == "Unsupported");
    CHECK(std::string(launchFailureName(LaunchFailure::EmuMissing)) == "EmuMissing");
    CHECK(std::string(launchFailureName(LaunchFailure::EmuInvalid)) == "EmuInvalid");
    CHECK(std::string(launchFailureName(LaunchFailure::RomMissing)) == "RomMissing");
    CHECK(std::string(launchFailureName(LaunchFailure::ArgsTooLong)) == "ArgsTooLong");
    CHECK(std::string(launchFailureName(LaunchFailure::NoMemory)) == "NoMemory");
}

TEST_CASE("Preflight schreibt nichts") {
    auto fs = fsWith(goodDol());
    REQUIRE(preflightEmulator(fs, nesGame(), "sd:", emus()).isOk());
    CHECK(fs.writes == 0);
}
