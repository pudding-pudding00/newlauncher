#include "core/wip_patch.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace nl;

TEST_CASE("parseWip: drei gültige Zeilen wie WiiFlow (offset original neu, Hex)") {
    const WipFile f = parseWip("001AB610 9421FFD0 4E800020\n001CED53 DA000000 71000000\r\n007AAC6A 7A6B6F6A 6F6A7A6B");
    REQUIRE(f.entries.size() == 3);
    CHECK(f.skipped == 0);
    CHECK(f.entries[0].offset == 0x001AB610u);
    CHECK(f.entries[0].original == 0x9421FFD0u);
    CHECK(f.entries[0].replacement == 0x4E800020u);
    CHECK(f.entries[1].offset == 0x001CED53u);  // ungerade Offsets sind erlaubt: der Booter patcht Byte für Byte
    CHECK(f.entries[2].replacement == 0x6F6A7A6Bu);
}

TEST_CASE("parseWip: Kommentare und Leerzeilen zählen nicht als übersprungen") {
    const WipFile f = parseWip("# Test-Patch\n\n   \n001AB610 9421FFD0 4E800020 # Rest der Zeile\n");
    REQUIRE(f.entries.size() == 1);
    CHECK(f.skipped == 0);
    CHECK(f.entries[0].replacement == 0x4E800020u);
}

TEST_CASE("parseWip: kaputtes Hex und falsche Feldzahl werden übersprungen, mit Zeilennummer") {
    const WipFile f = parseWip("001AB610 9421FFD0 4E80002G\n"   // kein Hex
                               "001AB610 9421FFD0\n"            // zwei Felder
                               "001AB610 9421FFD0 4E800020 1\n"  // vier Felder
                               "001AB610 9421FFD0 4E8000200\n"   // neun Ziffern
                               "0x001AB614 9421FFD0 4E800020\n");
    REQUIRE(f.entries.size() == 1);  // 0x-Präfix ist erlaubt (strtoul upstream)
    CHECK(f.entries[0].offset == 0x001AB614u);
    CHECK(f.skipped == 4);
    REQUIRE(f.badLines.size() == 4);
    CHECK(f.badLines[0] == 1);
    CHECK(f.badLines[3] == 4);
}

TEST_CASE("parseWip: Offset außerhalb des Hauptspeichers wird übersprungen") {
    // Der Booter vergleicht offset mit der Position im main.dol-Datenstrom; 0x80…-Adressen träfen nie.
    const WipFile f = parseWip("017FFFFC 00000000 00000001\n01800000 00000000 00000001\n80001000 00000000 00000001\n");
    REQUIRE(f.entries.size() == 1);
    CHECK(f.entries[0].offset == kWipMaxOffset - 4);
    CHECK(f.skipped == 2);
}

TEST_CASE("parseWip: höchstens 1024 Einträge, der Rest zählt als übersprungen") {
    std::string text;
    for (int i = 0; i < 1500; ++i) text += "00001000 00000000 00000001\n";
    const WipFile f = parseWip(text);
    CHECK(f.entries.size() == kWipMaxEntries);
    CHECK(kWipMaxEntries == 1024);
    CHECK(f.skipped == 1500 - 1024);
    CHECK(f.badLines.size() <= kWipMaxBadLines);
}

TEST_CASE("wipPath: <data>/codes/<ID6>.wip") {
    CHECK(wipPath("sd:/newlauncher", "RMGP01") == "sd:/newlauncher/codes/RMGP01.wip");
}
