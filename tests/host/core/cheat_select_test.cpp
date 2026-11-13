#include "core/cheat_select.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace nl;

TEST_CASE("parseSel/writeSel: Rundreise, getrimmt, ohne Leerzeilen") {
    const std::vector<std::string> names = parseSel("\xEF\xBB\xBF  Unendlich Leben \r\n\r\nZwei Zeilen\n");
    CHECK(names == std::vector<std::string>{"Unendlich Leben", "Zwei Zeilen"});
    CHECK(writeSel(names) == "Unendlich Leben\nZwei Zeilen\n");
    CHECK(parseSel(writeSel(names)) == names);
    CHECK(writeSel({}).empty());
}

TEST_CASE("selectedCodes: Dateireihenfolge, ohne Unbekanntes, Duplikate und Platzhalter") {
    GeckoFile f;
    f.id6 = "RMGP01";
    f.codes = {
        {"A", "", {0x04000000, 1}, false, ""},
        {"B", "", {0x04000004, 2}, true, ""},  // Platzhalter
        {"C", "", {0x04000008, 3}, false, ""},
        {"A", "", {0x0400000C, 4}, false, ""},  // gleicher Name: nur der erste
    };
    const std::vector<std::string> names{"C", "Weg", "A", "B", "A"};
    const auto sel = selectedCodes(f, names);
    REQUIRE(sel.size() == 2);
    CHECK(sel[0] == &f.codes[0]);
    CHECK(sel[1] == &f.codes[2]);
}

TEST_CASE("gctForSelection: nur gewählte, wählbare Codes; ohne Auswahl oder Liste leer") {
    const std::string txt = "RMGP01\nGalaxy\n\nEins\n04001000 00000063\n\nZwei\n04001004 000000FF\n";
    const std::vector<u8> gct = gctForSelection(txt, "Zwei\nGibtsNicht\n");
    REQUIRE(gct.size() == 24);  // Kopf 8 + ein Code 8 + Ende 8
    CHECK(gct[8] == 0x04);
    CHECK(gct[11] == 0x04);  // 04001004
    CHECK(gct[15] == 0xFF);
    CHECK(gctForSelection(txt, "").empty());
    CHECK(gctForSelection(txt, "GibtsNicht\n").empty());
    CHECK(gctForSelection("", "Zwei\n").empty());
    CHECK(gctForSelection("<html>kaputt</html>", "Zwei\n").empty());
}
