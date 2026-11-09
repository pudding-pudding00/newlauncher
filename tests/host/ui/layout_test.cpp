#include "ui/layout.hpp"
#include "ui/test_library.hpp"
#include <doctest/doctest.h>
#include <cstring>
#include <string_view>
#include <vector>
using namespace nl;
using namespace nl::ui;

TEST_CASE("gridMetrics füllt die Breite exakt") {
    for (float w : {640.0f, 854.0f}) {
        const GridMetrics m = gridMetrics(w);
        CHECK(m.columns == 6);
        CHECK(6 * m.tileW + 5 * m.gap + 2 * m.marginX == doctest::Approx(w));
        CHECK(m.topBarH == 56);
        CHECK(m.infoBarH == 72);
    }
    CHECK(gridMetrics(640).tileW == 88);
    CHECK(gridMetrics(640).tileH == 123);
    CHECK(gridMetrics(854).tileW == 104);
    CHECK(gridMetrics(854).tileH == 146);
}

TEST_CASE("Testbibliothek hat die geplanten Mengen") {
    const Library lib = testLibrary();
    int counts[kConsoleCount] = {};
    for (const Game& g : lib.games) ++counts[static_cast<int>(g.console)];
    const int expected[kConsoleCount] = {8, 10, 6, 20, 0, 9, 7, 14};
    for (int i = 0; i < kConsoleCount; ++i) CHECK(counts[i] == expected[i]);
    CHECK(lib.favorites.size() == 3);
    CHECK(lib.recent.empty());
}

TEST_CASE("buildBarEntries: Favoriten, Zuletzt, dann Konsolen nach Generation") {
    const Library lib = testLibrary();
    BarEntry e[kMaxBarEntries];
    const int n = buildBarEntries(lib, e);
    REQUIRE(n == 9);
    CHECK(e[0].kind == BarKind::Favorites);
    CHECK(e[1].kind == BarKind::Recent);
    const Console order[] = {Console::Nes, Console::Snes, Console::Gb, Console::N64,
                             Console::Gc, Console::Gba, Console::Wii};
    const bool group[] = {true, true, false, true, true, false, true};
    for (int i = 0; i < 7; ++i) {
        CHECK(e[2 + i].kind == BarKind::ConsoleEntry);
        CHECK(e[2 + i].console == order[i]);
        CHECK(e[2 + i].groupStart == group[i]);
    }
    CHECK_FALSE(e[0].groupStart);
}

TEST_CASE("buildBarEntries bei leerer Bibliothek: nur Favoriten und Zuletzt") {
    BarEntry e[kMaxBarEntries];
    CHECK(buildBarEntries(Library{}, e) == 2);
}

TEST_CASE("gamesFor liefert die Spiele einer Konsole bzw. Favoriten/Zuletzt") {
    const Library lib = testLibrary();
    std::vector<const Game*> scratch(lib.games.size());
    BarEntry n64{BarKind::ConsoleEntry, Console::N64, 5, true};
    auto games = gamesFor(lib, n64, scratch);
    CHECK(games.size() == 20);
    for (const Game* g : games) CHECK(g->console == Console::N64);
    CHECK(gamesFor(lib, BarEntry{BarKind::Favorites, Console::Nes, 0, false}, scratch).size() == 3);
    CHECK(gamesFor(lib, BarEntry{BarKind::Recent, Console::Nes, 0, false}, scratch).empty());
}

namespace {
GridModel model(int count, float width = 640) {
    GridModel g;
    g.reset(count, gridMetrics(width), 480 - 56 - 72);
    return g;
}
}  // namespace

TEST_CASE("GridModel.move klemmt an allen vier Rändern") {
    GridModel g = model(20);
    CHECK(g.focus() == 0);
    g.move(-1, 0); CHECK(g.focus() == 0);
    g.move(0, -1); CHECK(g.focus() == 0);
    g.setFocus(19);
    g.move(1, 0); CHECK(g.focus() == 19);
    g.move(0, 1); CHECK(g.focus() == 19);
    g.setFocus(5);
    g.move(1, 0); CHECK(g.focus() == 5);
    g.move(0, 1); CHECK(g.focus() == 11);
    g.move(-1, 0); CHECK(g.focus() == 10);
}

TEST_CASE("GridModel.move nach unten in unvollständige Zeile springt auf das letzte Element") {
    GridModel g = model(20);
    g.setFocus(17);
    g.move(0, 1);
    CHECK(g.focus() == 19);
}

TEST_CASE("GridModel mit 0 Elementen: focus -1, move ändert nichts, hitTest -1") {
    GridModel g = model(0);
    CHECK(g.focus() == -1);
    g.move(1, 1);
    CHECK(g.focus() == -1);
    CHECK(g.hitTest(100, 100, 0) == -1);
    CHECK(g.targetScroll() == 0);
}

TEST_CASE("Richtung nach Fokusverlust nimmt den letzten Fokus wieder auf") {
    GridModel g = model(20);
    g.setFocus(8);
    g.setFocus(-1);
    CHECK(g.focus() == -1);
    g.move(1, 0);
    CHECK(g.focus() == 8);
}

TEST_CASE("targetScroll hält die Fokuszeile sichtbar") {
    GridModel g = model(30);
    const float view = 480 - 56 - 72;
    g.setFocus(18);  // Zeile 3
    const float s = g.targetScroll();
    const gfx::Rect r = g.tileRect(18, s);
    CHECK(r.y >= 56);
    CHECK(r.y + r.h <= 56 + view);
    g.setFocus(0);
    CHECK(g.targetScroll() == 0);
}

TEST_CASE("hitTest trifft Kachel, nicht Lücke, nicht außerhalb des Rasterbereichs") {
    GridModel g = model(20);
    const gfx::Rect r = g.tileRect(7, 0);
    CHECK(g.hitTest(r.x + r.w / 2, r.y + r.h / 2, 0) == 7);
    CHECK(g.hitTest(r.x - 6, r.y + r.h / 2, 0) == -1);   // Lücke links
    CHECK(g.hitTest(r.x + r.w / 2, 20, 0) == -1);        // Konsolenleiste
    CHECK(g.hitTest(r.x + r.w / 2, 470, 0) == -1);       // Infoleiste
    CHECK(g.hitTest(5, r.y + 10, 0) == -1);              // Rand
}

TEST_CASE("hitTest auf Position hinter dem letzten Element liefert -1") {
    GridModel g = model(20);
    const gfx::Rect r = g.tileRect(23, 200);  // Zeile 3, Spalte 5: existiert nicht
    CHECK(g.hitTest(r.x + 5, r.y + 5, 200) == -1);
}

TEST_CASE("tileRect und hitTest sind bei Scroll konsistent") {
    GridModel g = model(30);
    for (float s : {0.0f, 50.0f, 135.0f}) {
        const gfx::Rect r = g.tileRect(13, s);
        if (r.y >= 56 && r.y + 10 < 480 - 72) CHECK(g.hitTest(r.x + 10, r.y + 10, s) == 13);
    }
}

TEST_CASE("infoLine") {
    char buf[64];
    CHECK(infoLine(Game{Console::N64, "k", "t", 1998, 1}, buf, sizeof buf) > 0);
    CHECK(std::string_view(buf) == "N64 · 1998 · 1 Spieler");
    infoLine(Game{Console::N64, "k", "t", 0, 1}, buf, sizeof buf);
    CHECK(std::string_view(buf) == "N64 · 1 Spieler");
    infoLine(Game{Console::Snes, "k", "t", 1991, 0}, buf, sizeof buf);
    CHECK(std::string_view(buf) == "SNES · 1991");
    char small[8];
    infoLine(Game{Console::N64, "k", "t", 1998, 1}, small, sizeof small);
    CHECK(std::strlen(small) == 7);
    CHECK(infoLine(Game{Console::N64, "k", "t", 1998, 1}, buf, 0) == 0);
}

TEST_CASE("formatClock") {
    char buf[8];
    formatClock(0, buf, sizeof buf); CHECK(std::string_view(buf) == "00:00");
    formatClock(1182, buf, sizeof buf); CHECK(std::string_view(buf) == "19:42");
    formatClock(1439, buf, sizeof buf); CHECK(std::string_view(buf) == "23:59");
    formatClock(1500, buf, sizeof buf); CHECK(std::string_view(buf) == "23:59");
    formatClock(-5, buf, sizeof buf); CHECK(std::string_view(buf) == "00:00");
}

TEST_CASE("formatDate") {
    char buf[64];
    formatDate(0, 9, 11, 2026, buf, sizeof buf);
    CHECK(std::string_view(buf) == "Montag, 9. November 2026");
    formatDate(9, 1, 13, 2026, buf, sizeof buf);
    CHECK(std::string_view(buf) == "Sonntag, 1. Dezember 2026");
}
