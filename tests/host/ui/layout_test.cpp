#include "ui/layout.hpp"
#include "ui/cover_provider.hpp"
#include "ui/strings.hpp"
#include "ui/test_library.hpp"
#include <doctest/doctest.h>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>
using namespace nl;
using namespace nl::ui;

TEST_CASE("gridMetrics füllt die Breite exakt") {
    for (float w : {640.0f, 854.0f}) {
        const GridMetrics m = gridMetrics(w);
        CHECK(m.columns == 6);
        CHECK(6 * m.tileW + 5 * m.gap + 2 * m.marginX == doctest::Approx(w));
        CHECK(m.topBarH == 48);
        CHECK(m.infoBarH == 60);
    }
    CHECK(gridMetrics(640).tileW == 99);
    CHECK(gridMetrics(640).tileH == 139);
    CHECK(gridMetrics(854).tileW == 128);
    CHECK(gridMetrics(854).tileH == 179);
}

TEST_CASE("gridMetrics: Kachelhöhe folgt dem Cover-Format, Breite bleibt (M23)") {
    CHECK(gridMetrics(854, 5.0f / 7.0f).tileH == 179);
    CHECK(gridMetrics(854, 1.0f).tileH == 128);
    CHECK(gridMetrics(854, 10.0f / 7.0f).tileH == 90);
    CHECK(gridMetrics(640, 1.0f).tileH == 99);
    CHECK(gridMetrics(640, 10.0f / 7.0f).tileH == 70);
    CHECK(gridMetrics(854, 10.0f / 7.0f).tileW == 128);
}

TEST_CASE("fitAspectRect: zentriert, ohne Verzerrung (M23)") {
    const gfx::Rect r{10, 20, 104, 73};
    const gfx::Rect q = fitAspectRect(r, 4, 10.0f / 7.0f);
    CHECK(q.w / q.h == doctest::Approx(10.0 / 7.0));
    CHECK(q.h == doctest::Approx(65.0f));
    CHECK(q.x + q.w / 2 == doctest::Approx(r.x + r.w / 2));
    CHECK(q.y + q.h / 2 == doctest::Approx(r.y + r.h / 2));
    const gfx::Rect p = fitAspectRect(gfx::Rect{0, 0, 104, 146}, 4, 1.0f);  // quadratisch in 5:7: Breite voll
    CHECK(p.w == doctest::Approx(96.0f));
    CHECK(p.h == doctest::Approx(96.0f));
}

TEST_CASE("Testbibliothek hat die geplanten Mengen") {
    const Library lib = testLibrary();
    int counts[kConsoleCount] = {};
    for (const Game& g : lib.games) ++counts[static_cast<int>(g.console)];
    const int expected[kConsoleCount] = {8, 10, 6, 20, 0, 9, 7, 14, 0};
    for (int i = 0; i < kConsoleCount; ++i) CHECK(counts[i] == expected[i]);
    CHECK(lib.favorites.size() == 3);
    CHECK(lib.recent.empty());
}

TEST_CASE("buildBarEntries: Favoriten, Zuletzt, dann Konsolen von neu nach alt") {
    const Library lib = testLibrary();
    BarEntry e[kMaxBarEntries];
    const int n = buildBarEntries(lib, e);
    REQUIRE(n == 9);
    CHECK(e[0].kind == BarKind::Favorites);
    CHECK(e[1].kind == BarKind::Recent);
    // Generation absteigend, innerhalb einer Generation Heimkonsole vor Handheld; GBC hat keine Spiele.
    const Console order[] = {Console::Wii, Console::Gc, Console::Gba, Console::N64,
                             Console::Snes, Console::Gb, Console::Nes};
    for (int i = 0; i < 7; ++i) {
        CHECK(e[2 + i].kind == BarKind::ConsoleEntry);
        CHECK(e[2 + i].console == order[i]);
    }
}

TEST_CASE("buildBarEntries bei leerer Bibliothek: nur Favoriten und Zuletzt") {
    BarEntry e[kMaxBarEntries];
    CHECK(buildBarEntries(Library{}, e) == 2);
}

TEST_CASE("gamesFor liefert die Spiele einer Konsole bzw. Favoriten/Zuletzt") {
    const Library lib = testLibrary();
    std::vector<const Game*> scratch(lib.games.size());
    BarEntry n64{BarKind::ConsoleEntry, Console::N64, 5};
    auto games = gamesFor(lib, n64, scratch);
    CHECK(games.size() == 20);
    for (const Game* g : games) CHECK(g->console == Console::N64);
    CHECK(gamesFor(lib, BarEntry{BarKind::Favorites, Console::Nes, 0}, scratch).size() == 3);
    CHECK(gamesFor(lib, BarEntry{BarKind::Recent, Console::Nes, 0}, scratch).empty());
}

namespace {
GridModel model(int count, float width = 640) {
    GridModel g;
    g.reset(count, gridMetrics(width), 480 - 48 - 60);
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
    const float view = 480 - 48 - 60;
    g.setFocus(18);  // Zeile 3
    const float s = g.targetScroll();
    const gfx::Rect r = g.tileRect(18, s);
    CHECK(r.y >= 48);
    CHECK(r.y + r.h <= 48 + view);
    g.setFocus(0);
    CHECK(g.targetScroll() == 0);
}

TEST_CASE("hitTest trifft Kachel, nicht Lücke, nicht außerhalb des Rasterbereichs") {
    GridModel g = model(20);
    const gfx::Rect r = g.tileRect(7, 0);
    CHECK(g.hitTest(r.x + r.w / 2, r.y + r.h / 2, 0) == 7);
    CHECK(g.hitTest(r.x - 2, r.y + r.h / 2, 0) == -1);   // Lücke links (4 px, M24)
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
        if (r.y >= 48 && r.y + 10 < 480 - 60) CHECK(g.hitTest(r.x + 10, r.y + 10, s) == 13);
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

TEST_CASE("formatDate folgt der Sprache") {
    const Lang saved = language();
    char buf[64];
    setLanguage(Lang::En);
    formatDate(0, 10, 11, 2026, buf, sizeof buf);
    CHECK(std::string_view(buf) == "Monday, November 10, 2026");
    setLanguage(Lang::De);
    formatDate(0, 10, 11, 2026, buf, sizeof buf);
    CHECK(std::string_view(buf) == "Montag, 10. November 2026");
    setLanguage(saved);
}

// M24 Variante B: gemischte Listen, Reihe so hoch wie ihre höchste Kachel, Kacheln vertikal zentriert.
namespace {
GridModel mixed(std::span<const float> heights, float view = 480 - 48 - 60) {
    GridModel g;
    g.reset(static_cast<int>(heights.size()), gridMetrics(854), view, heights);
    return g;
}
}  // namespace

TEST_CASE("GridModel: Reihenhöhe = höchste Kachel, kürzere vertikal zentriert (M24)") {
    const float h[] = {179, 90, 90, 90, 90, 90, 90, 90};
    const GridModel g = mixed(h);
    CHECK(g.rowHeight(0) == doctest::Approx(179));
    CHECK(g.rowHeight(1) == doctest::Approx(90));  // letzte, unvollständige Reihe
    const gfx::Rect a = g.tileRect(0, 0), b = g.tileRect(1, 0), c = g.tileRect(6, 0);
    CHECK(a.h == doctest::Approx(179));
    CHECK(b.h == doctest::Approx(90));
    CHECK(b.y + b.h / 2 == doctest::Approx(a.y + a.h / 2));
    CHECK(c.y == doctest::Approx(a.y + 179 + 4));
    CHECK(a.y == doctest::Approx(48 + 4));  // kGridTopPad 4
}

TEST_CASE("GridModel: hitTest in der Lücke über einer kurzen Kachel trifft nichts (M24)") {
    const float h[] = {179, 90, 90};
    const GridModel g = mixed(h);
    const gfx::Rect a = g.tileRect(0, 0), b = g.tileRect(1, 0);
    CHECK(g.hitTest(b.x + b.w / 2, b.y + b.h / 2, 0) == 1);
    CHECK(g.hitTest(b.x + b.w / 2, a.y + 5, 0) == -1);
    CHECK(g.hitTest(a.x + a.w / 2, a.y + 5, 0) == 0);
}

TEST_CASE("GridModel: maxScroll aus der Summe der Reihenhöhen, Fokus macht hohe Reihe ganz sichtbar (M24)") {
    std::vector<float> h;
    for (int row = 0; row < 5; ++row)
        for (int col = 0; col < 6; ++col) h.push_back(row % 2 == 0 ? 90.0f : 179.0f);
    const float view = 480 - 48 - 60;
    GridModel g = mixed(h, view);
    const float content = 4 + 3 * 90 + 2 * 179 + 4 * 4 + 4;
    CHECK(g.maxScroll() == doctest::Approx(content - view));
    g.setFocus(19);  // Reihe 3 (hoch)
    const gfx::Rect r = g.tileRect(19, g.targetScroll());
    CHECK(r.y >= 48 - 0.5f);
    CHECK(r.y + r.h <= 48 + view + 0.5f);
    CHECK(g.hitTest(r.x + 10, r.y + 10, g.targetScroll()) == 19);
    g.setFocus(29);
    CHECK(g.targetScroll() == doctest::Approx(g.maxScroll()));
}

TEST_CASE("GridModel ohne Höhen: alle Reihen tileH (Konsolenliste wie M23)") {
    GridModel g;
    g.reset(14, gridMetrics(854), 372);
    CHECK(g.rowHeight(0) == doctest::Approx(179));
    CHECK(g.rowHeight(2) == doctest::Approx(179));
    CHECK(g.tileRect(6, 0).y == doctest::Approx(g.tileRect(0, 0).y + 183));
}
