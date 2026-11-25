#pragma once
// Layout des Hauptbildschirms (Spec 3.1): Konsolenleiste, Cover-Raster mit 6 Spalten, Infoleiste.

#include "core/game.hpp"
#include "gfx/draw_list.hpp"

#include <cstddef>
#include <span>

namespace nl::ui {

struct Library {  // M1: Testdaten; ab M2 vom Scanner befüllt
    std::span<const Game> games;  // nach Konsole, dann Titel sortiert
    std::span<const Game* const> favorites;
    std::span<const Game* const> recent;
};

enum class BarKind : u8 { Favorites, Recent, ConsoleEntry, Search };
struct BarEntry {
    BarKind kind;
    Console console;
    u8 generation;
};
inline constexpr int kMaxBarEntries = 2 + kConsoleCount;

// Favoriten, Zuletzt, dann Konsolen mit mindestens einem Spiel von neu nach alt (Generation
// absteigend, innerhalb einer Generation Heimkonsole vor Handheld). Konsolen ohne Spiele fehlen.
int buildBarEntries(const Library& lib, std::span<BarEntry> out);
// scratch muss mindestens lib.games.size() Plätze haben.
std::span<const Game* const> gamesFor(const Library& lib, const BarEntry& e, std::span<const Game*> scratch);

struct GridMetrics {
    float tileW, tileH, gap, marginX, topBarH, infoBarH;
    int columns;
};
// M23: coverAspect (Breite/Höhe) bestimmt die Kachelhöhe; die Breite bleibt (6 Spalten).
GridMetrics gridMetrics(float logicalWidth, float coverAspect = 5.0f / 7.0f);

class GridModel {
public:
    void reset(int itemCount, const GridMetrics& m, float viewHeight);
    void move(int dx, int dy);
    // -1 = kein Fokus; der letzte Fokus bleibt für Richtungstasten gemerkt. Ohne scrollIntoView (Zeiger, M23)
    // bleibt die Scrollposition, sonst wird die Reihe ganz sichtbar.
    void setFocus(int index, bool scrollIntoView = true);
    void scrollBy(float dy);  // M23: Zeiger-Randscrollen, begrenzt auf [0, maxScroll()]
    float maxScroll() const;
    int focus() const { return focus_; }
    int count() const { return count_; }
    float targetScroll() const { return scroll_; }
    gfx::Rect tileRect(int index, float scroll) const;
    int hitTest(float x, float y, float scroll) const;
    float rowPitch() const { return m_.tileH + m_.gap; }
    float viewTop() const { return m_.topBarH; }
    float viewHeight() const { return viewHeight_; }

private:
    void updateScroll();

    GridMetrics m_{};
    float viewHeight_ = 0.0f;
    int count_ = 0, focus_ = -1, last_ = 0;
    float scroll_ = 0.0f;
};

// Alle Formatierer schreiben nullterminiert, kürzen bei kleinem Puffer und liefern die Länge.
int infoLine(const Game& g, char* out, std::size_t cap);           // "N64 · 1998 · 1 Spieler"
int formatClock(int minuteOfDay, char* out, std::size_t cap);      // "19:42"
int formatDate(int weekday, int day, int month, int year, char* out, std::size_t cap);

}  // namespace nl::ui
