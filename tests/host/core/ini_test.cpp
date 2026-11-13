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

TEST_CASE("rawLines liefert jede Inhaltszeile unverändert, auch mit =") {
    const IniDoc d = IniDoc::parse("[favorites]\n; Kommentar\nRMCP01\nnes/Mario; Luigi = Bros [!] Ä.nes\n");
    const auto raw = d.rawLines("favorites");
    REQUIRE(raw.size() == 2);
    CHECK(raw[0] == "RMCP01");
    CHECK(raw[1] == "nes/Mario; Luigi = Bros [!] Ä.nes");
}

TEST_CASE("entries liefert alle Paare in Dateireihenfolge, auch mit gleichem Schlüssel") {
    const IniDoc d = IniDoc::parse("[history]\n100 = a\n200 = b = c\n100 = d\n");
    const auto e = d.entries("history");
    REQUIRE(e.size() == 3);
    CHECK(e[1].first == "200");
    CHECK(e[1].second == "b = c");
    CHECK(e[2].second == "d");
}

TEST_CASE("IniDoc::sectionNames in Dateireihenfolge") {
    const IniDoc d = IniDoc::parse("a = 1\n[eins]\nx = 1\n[zwei]\n[eins]\ny = 2\n");
    const auto names = d.sectionNames();
    REQUIRE(names.size() == 2);  // Schlüssel vor dem ersten Abschnitt zählen nicht
    CHECK(names[0] == "eins");
    CHECK(names[1] == "zwei");
}

// M14: Textbearbeitung, die alles außerhalb der berührten Zeilen Byte für Byte erhält.
TEST_CASE("iniTextSet ersetzt den Wert an Ort und Stelle, Kommentare bleiben") {
    const std::string in = "; Kopf\r\n[Game RMGP01]\r\n# Notiz\r\nvideo = pal50\r\naspect = 16:9\r\n";
    CHECK(nl::iniTextSet(in, "game RMGP01", "VIDEO", "pal60") ==
          "; Kopf\r\n[Game RMGP01]\r\n# Notiz\r\nVIDEO = pal60\r\naspect = 16:9\r\n");
}

TEST_CASE("iniTextSet: doppelte Schlüssel werden zu einem, nullopt löscht") {
    const std::string in = "[gc]\nvideo = ntsc\nmemcard = multi\nvideo = pal50\n";
    CHECK(nl::iniTextSet(in, "gc", "video", "auto") == "[gc]\nmemcard = multi\nvideo = auto\n");
    CHECK(nl::iniTextSet(in, "gc", "video", std::nullopt) == "[gc]\nmemcard = multi\n");
}

TEST_CASE("iniTextSet hängt hinter der letzten Inhaltszeile an, sonst einen neuen Abschnitt") {
    CHECK(nl::iniTextSet("[wii]\nvideo = game\n\n[gc]\nx = 1\n", "wii", "aspect", "4:3") ==
          "[wii]\nvideo = game\naspect = 4:3\n\n[gc]\nx = 1\n");
    CHECK(nl::iniTextSet("[n64_method]\nn64/a.z64 = vc", "game RMGP01", "video", "pal60") ==
          "[n64_method]\nn64/a.z64 = vc\n\n[game RMGP01]\nvideo = pal60\n");
    CHECK(nl::iniTextSet("", "game RMGP01", "video", "pal60") == "[game RMGP01]\nvideo = pal60\n");
    CHECK(nl::iniTextSet("[a]\nb = 1\n", "game X", "video", std::nullopt) == "[a]\nb = 1\n");
}

TEST_CASE("iniTextRemoveSection entfernt alle gleichnamigen Abschnitte, der Rest bleibt") {
    const std::string in = "[n64_method]\r\nn64/a.z64 = vc\r\n\r\n[game  rmgp01]\r\nvideo = pal60\r\n; weg\r\n"
                           "[emulator]\r\nnes/a.nes = fceugx\r\n[GAME RMGP01]\r\nios = 249\r\n";
    CHECK(nl::iniTextRemoveSection(in, "game RMGP01") ==
          "[n64_method]\r\nn64/a.z64 = vc\r\n\r\n[emulator]\r\nnes/a.nes = fceugx\r\n");
}
