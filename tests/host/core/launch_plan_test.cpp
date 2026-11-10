#include "core/launch_args.hpp"
#include "core/launch_layout.hpp"
#include "core/launch_plan.hpp"
#include "support/dol_images.hpp"
#include "support/mem_fs.hpp"
#include <doctest/doctest.h>
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

TEST_CASE("DOL fehlt → NotFound mit DOL-Pfad") {
    test::MemFileSystem fs;
    fs.addFile("sd:/roms/nes/Zelda (Europe) (Rev 1).nes", {});
    auto r = preflightEmulator(fs, nesGame(), "sd:", emus());
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().code == ErrorCode::NotFound);
    CHECK(r.error().context == kDol);
}

TEST_CASE("DOL 0 Byte bzw. Müll → InvalidData") {
    auto empty = fsWith({});
    auto r = preflightEmulator(empty, nesGame(), "sd:", emus());
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().code == ErrorCode::InvalidData);
    CHECK(r.error().context == kDol);
    auto garbage = fsWith(std::vector<u8>(0x400, 0xAB));
    auto g = preflightEmulator(garbage, nesGame(), "sd:", emus());
    REQUIRE_FALSE(g.isOk());
    CHECK(g.error().code == ErrorCode::InvalidData);
}

TEST_CASE("DOL größer als kMaxDolBytes → InvalidData") {
    // Sektion endet weit hinter dem Header, Datei ist entsprechend groß; Größe kommt aus list().
    auto img = test::dolImage({{0x100, 0x80004000, 0x200}}, {}, 0x80004000);
    img.resize(kMaxDolBytes + 1, 0);
    auto fs = fsWith(std::move(img));
    auto r = preflightEmulator(fs, nesGame(), "sd:", emus());
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().code == ErrorCode::InvalidData);
    CHECK(r.error().context == kDol);
}

TEST_CASE("ROM fehlt → NotFound mit ROM-Pfad") {
    test::MemFileSystem fs;
    fs.addFile(kDol, goodDol());
    auto r = preflightEmulator(fs, nesGame(), "sd:", emus());
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().code == ErrorCode::NotFound);
    CHECK(r.error().context == "sd:/roms/nes/Zelda (Europe) (Rev 1).nes");
}

TEST_CASE("N64/GC/Wii → Unsupported mit Konsolenname") {
    auto fs = fsWith(goodDol());
    for (Console c : {Console::N64, Console::Gc, Console::Wii}) {
        auto r = preflightEmulator(fs, nesGame("roms/n64/x.z64", c), "sd:", emus());
        REQUIRE_FALSE(r.isOk());
        CHECK(r.error().code == ErrorCode::Unsupported);
        CHECK(r.error().context == consoleInfo(c).name);
    }
}

TEST_CASE("argv zu lang → InvalidData") {
    auto fs = fsWith(goodDol());
    const EmulatorTable t = EmulatorTable::fromIni(IniDoc::parse("[nes]\nargs = " + std::string(kMaxArgvBytes, 'x') + "\n"));
    auto r = preflightEmulator(fs, nesGame(), "sd:", t);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().code == ErrorCode::InvalidData);
}

TEST_CASE("Preflight schreibt nichts") {
    auto fs = fsWith(goodDol());
    REQUIRE(preflightEmulator(fs, nesGame(), "sd:", emus()).isOk());
    CHECK(fs.writes == 0);
}
