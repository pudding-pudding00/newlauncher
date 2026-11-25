#include "ui/layout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "ui/strings.hpp"

namespace nl::ui {

namespace {
constexpr float kGridTopPad = 12.0f;

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

int writef(char* out, std::size_t cap, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
int writef(char* out, std::size_t cap, const char* fmt, ...) {
    if (cap == 0) return 0;
    va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(out, cap, fmt, args);
    va_end(args);
    if (n < 0) {
        out[0] = '\0';
        return 0;
    }
    return n < static_cast<int>(cap) ? n : static_cast<int>(cap) - 1;
}
}  // namespace

int buildBarEntries(const Library& lib, std::span<BarEntry> out) {
    int n = 0;
    auto add = [&](BarEntry e) {
        if (n < static_cast<int>(out.size())) out[n++] = e;
    };
    add(BarEntry{BarKind::Favorites, Console::Nes, 0});
    add(BarEntry{BarKind::Recent, Console::Nes, 0});
    // Die Konsolentabelle ist nach Generation aufsteigend sortiert, Heimkonsole vor Handheld.
    // Generationen werden rückwärts durchlaufen, innerhalb einer Generation vorwärts.
    const int lastGen = consoleInfo(static_cast<Console>(kConsoleCount - 1)).generation;
    const int firstGen = consoleInfo(static_cast<Console>(0)).generation;
    for (int gen = lastGen; gen >= firstGen; --gen) {
        for (int c = 0; c < kConsoleCount; ++c) {
            const Console console = static_cast<Console>(c);
            if (consoleInfo(console).generation != gen) continue;
            bool any = false;
            for (const Game& g : lib.games) {
                if (g.console == console) {
                    any = true;
                    break;
                }
            }
            if (any) add(BarEntry{BarKind::ConsoleEntry, console, static_cast<u8>(gen)});
        }
    }
    return n;
}

std::span<const Game* const> gamesFor(const Library& lib, const BarEntry& e, std::span<const Game*> scratch) {
    if (e.kind == BarKind::Favorites) return lib.favorites;
    if (e.kind == BarKind::Recent) return lib.recent;
    std::size_t n = 0;
    for (const Game& g : lib.games) {
        if (g.console == e.console && n < scratch.size()) scratch[n++] = &g;
    }
    return scratch.first(n);
}

GridMetrics gridMetrics(float logicalWidth, float coverAspect) {
    GridMetrics m = logicalWidth >= 854.0f ? GridMetrics{104, 146, 16, 75, 56, 72, 6} : GridMetrics{88, 123, 12, 26, 56, 72, 6};
    // Die 5:7-Höhe ist die Grundform; andere Formate skalieren sie (quer 10:7 → halbe Höhe).
    if (coverAspect > 0.0f) m.tileH = std::round(m.tileH * (5.0f / 7.0f) / coverAspect);
    return m;
}

void GridModel::reset(int itemCount, const GridMetrics& m, float viewHeight) {
    m_ = m;
    viewHeight_ = viewHeight;
    count_ = itemCount > 0 ? itemCount : 0;
    focus_ = count_ > 0 ? 0 : -1;
    last_ = 0;
    scroll_ = 0.0f;
}

void GridModel::setFocus(int index, bool scrollIntoView) {
    if (index < 0 || index >= count_) {
        focus_ = -1;
        return;
    }
    focus_ = last_ = index;
    if (scrollIntoView) updateScroll();
}

float GridModel::maxScroll() const {
    if (count_ == 0 || m_.columns <= 0) return 0.0f;
    const int rows = (count_ + m_.columns - 1) / m_.columns;
    const float content = 2 * kGridTopPad + static_cast<float>(rows) * rowPitch() - m_.gap;
    return content > viewHeight_ ? content - viewHeight_ : 0.0f;
}

void GridModel::scrollBy(float dy) { scroll_ = std::clamp(scroll_ + dy, 0.0f, maxScroll()); }

void GridModel::move(int dx, int dy) {
    if (count_ == 0) return;
    if (focus_ < 0) {
        setFocus(last_ < count_ ? last_ : 0);
        return;
    }
    const int cols = m_.columns;
    int row = focus_ / cols, col = focus_ % cols;
    if (dx != 0) {
        const int rowEnd = clampi(row * cols + cols - 1, 0, count_ - 1);
        col = clampi(col + dx, 0, rowEnd - row * cols);
    }
    if (dy != 0) {
        const int lastRow = (count_ - 1) / cols;
        const int newRow = row + dy;
        if (newRow >= 0 && newRow <= lastRow) row = newRow;
    }
    setFocus(clampi(row * cols + col, 0, count_ - 1));
}

void GridModel::updateScroll() {
    if (focus_ < 0) return;
    const float pitch = rowPitch();
    const float rowTop = kGridTopPad + static_cast<float>(focus_ / m_.columns) * pitch;
    const float rowBottom = rowTop + m_.tileH;
    if (rowTop - kGridTopPad < scroll_) scroll_ = rowTop - kGridTopPad;
    if (rowBottom + kGridTopPad > scroll_ + viewHeight_) scroll_ = rowBottom + kGridTopPad - viewHeight_;
    if (scroll_ < 0.0f) scroll_ = 0.0f;
}

gfx::Rect GridModel::tileRect(int index, float scroll) const {
    const int cols = m_.columns > 0 ? m_.columns : 1;
    const int row = index / cols, col = index % cols;
    return gfx::Rect{m_.marginX + static_cast<float>(col) * (m_.tileW + m_.gap),
                     m_.topBarH + kGridTopPad + static_cast<float>(row) * rowPitch() - scroll, m_.tileW, m_.tileH};
}

int GridModel::hitTest(float x, float y, float scroll) const {
    if (count_ == 0 || y < m_.topBarH || y >= m_.topBarH + viewHeight_) return -1;
    const float lx = x - m_.marginX;
    const float ly = y - m_.topBarH - kGridTopPad + scroll;
    if (lx < 0.0f || ly < 0.0f) return -1;
    const int col = static_cast<int>(lx / (m_.tileW + m_.gap));
    const int row = static_cast<int>(ly / rowPitch());
    if (col >= m_.columns) return -1;
    if (lx - static_cast<float>(col) * (m_.tileW + m_.gap) > m_.tileW) return -1;
    if (ly - static_cast<float>(row) * rowPitch() > m_.tileH) return -1;
    const int index = row * m_.columns + col;
    return index < count_ ? index : -1;
}

int infoLine(const Game& g, char* out, std::size_t cap) {
    const char* label = consoleLabel(g.console);
    if (g.year != 0 && g.players != 0)
        return writef(out, cap, "%s \xC2\xB7 %u \xC2\xB7 %u %s", label, g.year, g.players, tr(Str::Player));
    if (g.year != 0) return writef(out, cap, "%s \xC2\xB7 %u", label, g.year);
    if (g.players != 0) return writef(out, cap, "%s \xC2\xB7 %u %s", label, g.players, tr(Str::Player));
    return writef(out, cap, "%s", label);
}

int formatClock(int minuteOfDay, char* out, std::size_t cap) {
    const int m = clampi(minuteOfDay, 0, 24 * 60 - 1);
    return writef(out, cap, "%02d:%02d", m / 60, m % 60);
}

int formatDate(int weekday, int day, int month, int year, char* out, std::size_t cap) {
    if (language() == Lang::En) {
        return writef(out, cap, "%s, %s %d, %d", weekdayName(weekday), monthName(month), clampi(day, 1, 31), year);
    }
    return writef(out, cap, "%s, %d. %s %d", weekdayName(weekday), clampi(day, 1, 31), monthName(month), year);
}

}  // namespace nl::ui
