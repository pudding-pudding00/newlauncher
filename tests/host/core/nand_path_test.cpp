#include "core/nand_path.hpp"
#include <doctest/doctest.h>
using namespace nl;

TEST_CASE("isfsPath: nur nand:-Pfade, ohne ..") {
    CHECK(isfsPath("nand:/title/00010000/524d4750/data/a.bin") == std::optional<std::string>("/title/00010000/524d4750/data/a.bin"));
    CHECK(!isfsPath("sd:/title/x").has_value());
    CHECK(!isfsPath("nand:title").has_value());
    CHECK(!isfsPath("nand:/title/../shared2").has_value());
}

TEST_CASE("isSaveDataPath: nur unter /title/<hi>/<lo>/data") {
    CHECK(isSaveDataPath("/title/00010000/524d4750/data/a.bin"));
    CHECK(isSaveDataPath("/title/00010001/4e414c45/data/sub/c.bin"));
    CHECK(isSaveDataPath("/title/00010000/524D4750/data/a.bin"));  // Großbuchstaben-Hex ist auch Hex
    CHECK(!isSaveDataPath("/title/00010000/524d4750/data"));        // der Ordner selbst ist keine Datei
    CHECK(!isSaveDataPath("/title/00010000/524d4750/content/00000000.app"));
    CHECK(!isSaveDataPath("/title/00000001/00000002/datax/a"));
    CHECK(!isSaveDataPath("/title/0001000/524d4750/data/a"));
    CHECK(!isSaveDataPath("/title/0001000g/524d4750/data/a"));
    CHECK(!isSaveDataPath("/shared2/sys/SYSCONF"));
    CHECK(!isSaveDataPath("/title/00010000/524d4750/data//a"));
    CHECK(!isSaveDataPath("/title/00010000/524d4750/data/sub/../../content/x"));
}

TEST_CASE("isSaveDataDir: Ordner data selbst und darunter") {
    CHECK(isSaveDataDir("/title/00010000/524d4750/data"));
    CHECK(isSaveDataDir("/title/00010000/524d4750/data/sub"));
    CHECK(!isSaveDataDir("/title/00010000/524d4750"));
    CHECK(!isSaveDataDir("/title/00010000/524d4750/content"));
}

TEST_CASE("NAND-Grenzen: Namen höchstens 12 Zeichen, Pfad höchstens 64") {
    CHECK(isSaveDataPath("/title/00010000/524d4750/data/abcdefgh.bin"));    // 12
    CHECK(!isSaveDataPath("/title/00010000/524d4750/data/abcdefghi.bin"));  // 13
    std::string deep = "/title/00010000/524d4750/data";
    while (deep.size() + 4 <= 64) deep += "/abc";
    CHECK(isSaveDataPath(deep));
    CHECK(!isSaveDataPath(deep + "/abc"));
}

TEST_CASE("nandTempPath: /tmp mit demselben Dateinamen (IOS benennt Dateien nur ohne Namenswechsel um)") {
    CHECK(nandTempPath("/title/00010000/524d4750/data/a.bin") == "/tmp/a.bin");
    CHECK(nandTempPath("/title/00010000/524d4750/data/sub/smoke.sav") == "/tmp/smoke.sav");
}
