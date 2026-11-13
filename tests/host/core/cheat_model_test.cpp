#include "core/cheat_model.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace nl;

namespace {

constexpr std::string_view kTxt =
    "RMCP01\n"
    "Mario Kart Wii\n"
    "\n"
    "Alpha [Ich]\n"
    "04000000 00000001\n"
    "\n"
    "Platzhalter\n"
    "04000000 0000XXXX\n"
    "\n"
    "Beta\n"
    "04000004 00000002\n"
    "\n"
    "Alpha\n"
    "04000008 00000003\n";

CheatModel wii(std::string_view sel = "") { return CheatModel(Console::Wii, kTxt, sel, true); }

}  // namespace

TEST_CASE("CheatModel: Zeilen in Dateireihenfolge, Platzhalter nicht wählbar") {
    const CheatModel m = wii();
    CHECK(m.status() == CheatStatus::Ready);
    REQUIRE(m.rows() == 4);
    CHECK(m.label(0) == "Alpha");
    CHECK(m.author(0) == "Ich");
    CHECK(m.selectable(0));
    CHECK_FALSE(m.selectable(1));
    CHECK(m.selectable(2));
    CHECK_FALSE(m.selectable(3));  // doppelter Name: nur der erste zählt (.sel speichert Namen)
    CHECK(m.title() == "Mario Kart Wii");
}

TEST_CASE("CheatModel: toggle auf nicht wählbare Zeile ändert nichts") {
    CheatModel m = wii();
    CHECK_FALSE(m.toggle(1));
    CHECK_FALSE(m.checked(1));
    CHECK_FALSE(m.toggle(3));
    CHECK_FALSE(m.toggle(99));
    CHECK_FALSE(m.dirty());
    CHECK(m.selection().empty());
}

TEST_CASE("CheatModel: selection in Dateireihenfolge, .sel-Text") {
    CheatModel m = wii();
    CHECK(m.toggle(2));
    CHECK(m.toggle(0));
    CHECK(m.dirty());
    CHECK(m.selection() == std::vector<std::string>{"Alpha", "Beta"});
    CHECK(m.selText() == "Alpha\nBeta\n");
    CHECK(m.toggle(0));
    CHECK(m.selection() == std::vector<std::string>{"Beta"});
}

TEST_CASE("CheatModel: .sel setzt Haken; Unbekanntes und Platzhalter fallen still weg") {
    const CheatModel m = wii("\xEF\xBB\xBF" "Beta\nGibtsNicht\nPlatzhalter\nAlpha\nAlpha\n");
    CHECK(m.checked(0));
    CHECK_FALSE(m.checked(1));
    CHECK(m.checked(2));
    CHECK_FALSE(m.checked(3));
    CHECK(m.selection() == std::vector<std::string>{"Alpha", "Beta"});
    CHECK_FALSE(m.dirty());
}

TEST_CASE("CheatModel: keine oder kaputte Datei") {
    CHECK(CheatModel(Console::Wii, "", "", true).status() == CheatStatus::NoFile);
    CHECK(CheatModel(Console::Gc, "<html>404</html>", "", true).status() == CheatStatus::NoFile);
    CHECK(CheatModel(Console::Gc, "", "Alpha\n", true).rows() == 0);
}

TEST_CASE("CheatModel: Wii ohne Booter-Cheats → WiiUnavailable, GC unberührt") {
    const CheatModel w(Console::Wii, kTxt, "Alpha\n", false);
    CHECK(w.status() == CheatStatus::WiiUnavailable);
    CHECK(w.rows() == 0);
    CHECK(CheatModel(Console::Gc, kTxt, "", false).status() == CheatStatus::Ready);
}

TEST_CASE("CheatModel: übersprungene Blöcke werden gezählt") {
    const CheatModel m(Console::Gc, "GM8P01\nMetroid\n\nKaputt\n\nGut\n00000000 00000001\n", "", true);
    CHECK(m.rows() == 1);
    CHECK(m.skipped() == 1);
}
