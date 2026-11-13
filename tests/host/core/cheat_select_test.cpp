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
