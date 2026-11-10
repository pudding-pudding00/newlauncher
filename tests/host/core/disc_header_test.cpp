#include "core/disc_header.hpp"
#include "support/disc_images.hpp"
#include "support/mem_fs.hpp"
#include <doctest/doctest.h>
#include <cstring>
using namespace nl;

TEST_CASE("parseWiiIso liest ID6 und Titel") {
    const auto img = test::wiiIso("RMCP01", "Mario Kart Wii");
    const auto info = parseWiiIso(img);
    REQUIRE(info.has_value());
    CHECK(std::strcmp(info->id6, "RMCP01") == 0);
    CHECK(info->title == "Mario Kart Wii");
}

TEST_CASE("parseGcIso liest ID6 und Titel") {
    const auto img = test::gcIso("GM8P01", "Metroid Prime");
    const auto info = parseGcIso(img);
    REQUIRE(info.has_value());
    CHECK(std::strcmp(info->id6, "GM8P01") == 0);
    CHECK(info->title == "Metroid Prime");
    CHECK_FALSE(parseWiiIso(img).has_value());  // GC-Image ist kein Wii-Image
}

TEST_CASE("parseWbfs liest den Disc-Header am Sektor-Offset") {
    const auto img = test::wbfsFile("RMGP01", "Super Mario Galaxy");
    const auto info = parseWbfs(img);
    REQUIRE(info.has_value());
    CHECK(std::strcmp(info->id6, "RMGP01") == 0);
    CHECK(info->title == "Super Mario Galaxy");
}

TEST_CASE("ungültige Header ergeben nullopt") {
    auto img = test::wiiIso("RMCP01", "x");
    img[0x18] = 0;  // Magic kaputt
    CHECK_FALSE(parseWiiIso(img).has_value());
    CHECK_FALSE(parseWiiIso(test::wiiIso("rmcp01", "x")).has_value());       // Kleinbuchstaben
    CHECK_FALSE(parseWiiIso(test::wiiIso(std::string_view("RMC\x01P0", 6), "x")).has_value());
    const auto full = test::wiiIso("RMCP01", "x");
    CHECK_FALSE(parseWiiIso(std::span<const u8>(full.data(), 100)).has_value());  // zu kurz
    CHECK_FALSE(parseWbfs(test::wbfsFile("RMGP01", "x", 10, 2048)).has_value());  // hd_sec_sz_s > 9
    CHECK_FALSE(parseWbfs(test::wiiIso("RMCP01", "x")).has_value());              // kein WBFS-Magic
    std::vector<u8> garbage(1024, 0xAB);
    CHECK_FALSE(parseWbfs(garbage).has_value());
    CHECK_FALSE(parseGcIso(garbage).has_value());
}

TEST_CASE("Titel ohne 0-Terminator wird bei 64 Byte gekappt; Byte 0xE9 wird zu é") {
    std::string longTitle(80, 'A');
    const auto info = parseWiiIso(test::wiiIso("RMCP01", longTitle));
    REQUIRE(info.has_value());
    CHECK(info->title.size() == 64);
    const auto accent = parseGcIso(test::gcIso("GM8P01", "Pok\xE9mon"));
    REQUIRE(accent.has_value());
    CHECK(accent->title == "Pok\xC3\xA9mon");
}

TEST_CASE("MemFileSystem: list, read mit Offset hinter dem Ende, writeAtomic, makeDirs") {
    test::MemFileSystem fs;
    fs.addFile("sd:/roms/nes/a.nes", {1, 2, 3}, 5);
    fs.addDir("sd:/roms/gbc", 6);
    auto top = fs.list("sd:/roms");
    REQUIRE(top.isOk());
    CHECK(top.value().size() == 2);
    auto nes = fs.list("sd:/roms/nes");
    REQUIRE(nes.isOk());
    REQUIRE(nes.value().size() == 1);
    CHECK(nes.value()[0].name == "a.nes");
    CHECK_FALSE(nes.value()[0].isDir);
    CHECK(nes.value()[0].size == 3);
    CHECK(nes.value()[0].mtime == 5);
    CHECK_FALSE(fs.list("sd:/fehlt").isOk());

    u8 buf[4] = {};
    auto n = fs.read("sd:/roms/nes/a.nes", 1, buf);
    REQUIRE(n.isOk());
    CHECK(n.value() == 2);
    CHECK(buf[0] == 2);
    auto past = fs.read("sd:/roms/nes/a.nes", 10, buf);
    REQUIRE(past.isOk());
    CHECK(past.value() == 0);
    CHECK_FALSE(fs.read("sd:/fehlt.nes", 0, buf).isOk());

    REQUIRE(fs.makeDirs("sd:/newlauncher/cache").isOk());
    const u8 data[] = {9, 8};
    REQUIRE(fs.writeAtomic("sd:/newlauncher/cache/index.bin", data).isOk());
    auto all = fs.readAll("sd:/newlauncher/cache/index.bin", 100);
    REQUIRE(all.isOk());
    CHECK(all.value().size() == 2);
    CHECK_FALSE(fs.readAll("sd:/newlauncher/cache/index.bin", 1).isOk());  // größer als erlaubt
}

TEST_CASE("readAllWithFallback: Hauptdatei bevorzugt, sonst die .tmp aus einem abgebrochenen Schreibvorgang") {
    test::MemFileSystem fs;
    fs.addFile("sd:/newlauncher/favorites.ini.tmp", {'t'});
    auto onlyTmp = readAllWithFallback(fs, "sd:/newlauncher/favorites.ini", 100);
    REQUIRE(onlyTmp.isOk());
    CHECK(onlyTmp.value() == std::vector<u8>{'t'});
    fs.addFile("sd:/newlauncher/favorites.ini", {'m'});
    auto both = readAllWithFallback(fs, "sd:/newlauncher/favorites.ini", 100);
    REQUIRE(both.isOk());
    CHECK(both.value() == std::vector<u8>{'m'});
    CHECK_FALSE(readAllWithFallback(fs, "sd:/fehlt.ini", 100).isOk());
}
