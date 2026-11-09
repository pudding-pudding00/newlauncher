#include "core/ini.hpp"
#include <doctest/doctest.h>
#include <string>
using nl::IniDoc;

TEST_CASE("Schlüssel ist alles vor dem ersten =") {
    const IniDoc d = IniDoc::parse("[s]\na = b = c\n");
    CHECK(d.get("s", "a") == "b = c");
}

TEST_CASE("Ränder werden getrimmt, Leerzeichen im Schlüssel bleiben") {
    const IniDoc d = IniDoc::parse("[map]\n  Super Mario 64 (Europe).z64 =   000100014E414D50  \n");
    CHECK(d.get("map", "Super Mario 64 (Europe).z64") == "000100014E414D50");
}

TEST_CASE("nur ganze Zeilen mit ; oder # sind Kommentare") {
    const IniDoc d = IniDoc::parse("[s]\n; kommentar = 1\n  # auch = 2\nrom;1.nes = x # kein Kommentar\n");
    CHECK(d.get("s", "rom;1.nes") == "x # kein Kommentar");
    CHECK(d.get("s", "; kommentar", "fehlt") == "fehlt");
    CHECK(d.get("s", "# auch", "fehlt") == "fehlt");
}

TEST_CASE("Zeilen ohne = landen in lines() des Abschnitts") {
    const IniDoc d = IniDoc::parse("[favorites]\nRMCP01\n  nes/Super Mario Bros. (World).nes  \n");
    const auto lines = d.lines("favorites");
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "RMCP01");
    CHECK(lines[1] == "nes/Super Mario Bros. (World).nes");
    CHECK(d.lines("fehlt").empty());
}

TEST_CASE("CRLF, BOM am Anfang und fehlender Zeilenumbruch am Ende") {
    const IniDoc d = IniDoc::parse("\xEF\xBB\xBF[general]\r\nlanguage = de\r\nlibrary = sd");
    CHECK(d.get("general", "language") == "de");
    CHECK(d.get("general", "library") == "sd");
}

TEST_CASE("Werte vor dem ersten Abschnitt und leere Abschnittsnamen werden ignoriert") {
    const IniDoc d = IniDoc::parse("los = 1\n[]\nauch = 2\n[s]\nok = 3\n");
    CHECK(d.get("", "los", "x") == "x");
    CHECK(d.get("", "auch", "x") == "x");
    CHECK(d.get("s", "ok") == "3");
}

TEST_CASE("Zeile mit [ am Anfang und = ist ein Schlüssel, kein Abschnitt") {
    const IniDoc d = IniDoc::parse("[s]\n[!] Tetris.gb = 1\n");
    CHECK(d.get("s", "[!] Tetris.gb") == "1");
}

TEST_CASE("set ersetzt vorhandenen Schlüssel und hängt neuen an; Round-Trip") {
    IniDoc d = IniDoc::parse("[a]\nx = 1\n");
    d.set("a", "x", "2");
    d.set("a", "y", "3");
    d.set("b", "z", "4");
    d.addLine("c", "nur zeile");
    const IniDoc r = IniDoc::parse(d.serialize());
    CHECK(r.get("a", "x") == "2");
    CHECK(r.get("a", "y") == "3");
    CHECK(r.get("b", "z") == "4");
    REQUIRE(r.lines("c").size() == 1);
    CHECK(r.lines("c")[0] == "nur zeile");
    CHECK(d.serialize() == "[a]\nx = 2\ny = 3\n\n[b]\nz = 4\n\n[c]\nnur zeile\n");
}

TEST_CASE("Umlaute und [ ] im Schlüssel überstehen den Round-Trip") {
    IniDoc d;
    d.set("map", "Zelda [!] Ä; = x.z64", "Wert = mit = Gleich");
    const IniDoc r = IniDoc::parse(d.serialize());
    CHECK(r.get("map", "Zelda [!] Ä; = x.z64", "fehlt") == "fehlt");  // Schlüssel endet am ersten =
    CHECK(r.get("map", "Zelda [!] Ä;") == "x.z64 = Wert = mit = Gleich");
}

TEST_CASE("doppelte Abschnitte werden zusammengeführt") {
    const IniDoc d = IniDoc::parse("[s]\na = 1\n[t]\n[s]\nb = 2\n");
    CHECK(d.get("s", "a") == "1");
    CHECK(d.get("s", "b") == "2");
}
