#include "ui/layout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "ui/strings.hpp"

namespace nl::ui {

namespace {
constexpr float kGridTopPad = 4.0f;  // M24: Luft über und unter dem Raster

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
    GridMetrics m = logicalWidth >= 854.0f ? GridMetrics{128, 179, 4, 33, 48, 60, 6} : GridMetrics{99, 139, 4, 13, 48, 60, 6};  // M24
    // Die 5:7-Höhe ist die Grundform; andere Formate skalieren sie (quer 10:7 → halbe Höhe).
    if (coverAspect > 0.0f) m.tileH = std::round(m.tileH * (5.0f / 7.0f) / coverAspect);
    return m;
}

void GridModel::reset(int itemCount, const GridMetrics& m, float viewHeight, std::span<const float> itemHeights) {
    m_ = m;
    viewHeight_ = viewHeight;
    count_ = itemCount > 0 ? itemCount : 0;
    focus_ = count_ > 0 ? 0 : -1;
    last_ = 0;
    scroll_ = 0.0f;
    heights_.assign(itemHeights.begin(), itemHeights.end());
    const int cols = m_.columns > 0 ? m_.columns : 1;
    const int rows = (count_ + cols - 1) / cols;
    rowTop_.assign(static_cast<std::size_t>(rows), 0.0f);
    rowH_.assign(static_cast<std::size_t>(rows), 0.0f);
    float y = kGridTopPad;
    for (int r = 0; r < rows; ++r) {
        float h = 0.0f;
        for (int i = r * cols; i < count_ && i < (r + 1) * cols; ++i) h = std::max(h, itemHeight(i));
        rowTop_[static_cast<std::size_t>(r)] = y;
        rowH_[static_cast<std::size_t>(r)] = h;
        y += h + m_.gap;
    }
}

float GridModel::itemHeight(int index) const {
    if (index >= 0 && static_cast<std::size_t>(index) < heights_.size() && heights_[static_cast<std::size_t>(index)] > 0.0f)
        return heights_[static_cast<std::size_t>(index)];
    return m_.tileH;
}

float GridModel::rowHeight(int row) const {
    return row >= 0 && row < rowCount() ? rowH_[static_cast<std::size_t>(row)] : m_.tileH;
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
    if (rowCount() == 0) return 0.0f;
    const float content = rowTop_.back() + rowH_.back() + kGridTopPad;
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
    const int row = focus_ / (m_.columns > 0 ? m_.columns : 1);
    if (row >= rowCount()) return;
    const float rowTop = rowTop_[static_cast<std::size_t>(row)];
    const float rowBottom = rowTop + rowH_[static_cast<std::size_t>(row)];
    if (rowTop - kGridTopPad < scroll_) scroll_ = rowTop - kGridTopPad;
    if (rowBottom + kGridTopPad > scroll_ + viewHeight_) scroll_ = rowBottom + kGridTopPad - viewHeight_;
    if (scroll_ < 0.0f) scroll_ = 0.0f;
}

gfx::Rect GridModel::tileRect(int index, float scroll) const {
    const int cols = m_.columns > 0 ? m_.columns : 1;
    const int row = index / cols, col = index % cols;
    float top = 0.0f, rowH = m_.tileH;
    if (row < rowCount()) {
        top = rowTop_[static_cast<std::size_t>(row)];
        rowH = rowH_[static_cast<std::size_t>(row)];
    } else {  // hinter dem letzten Element: Grundmaß fortsetzen
        const float end = rowCount() > 0 ? rowTop_.back() + rowH_.back() + m_.gap : kGridTopPad;
        top = end + static_cast<float>(row - rowCount()) * rowPitch();
    }
    const float h = index < count_ ? itemHeight(index) : m_.tileH;
    return gfx::Rect{m_.marginX + static_cast<float>(col) * (m_.tileW + m_.gap),
                     m_.topBarH + top + (rowH - h) / 2.0f - scroll, m_.tileW, h};
}

int GridModel::hitTest(float x, float y, float scroll) const {
    if (count_ == 0 || y < m_.topBarH || y >= m_.topBarH + viewHeight_) return -1;
    const float lx = x - m_.marginX;
    if (lx < 0.0f) return -1;
    const int col = static_cast<int>(lx / (m_.tileW + m_.gap));
    if (col >= m_.columns) return -1;
    if (lx - static_cast<float>(col) * (m_.tileW + m_.gap) > m_.tileW) return -1;
    const float ly = y - m_.topBarH + scroll;
    for (int row = 0; row < rowCount(); ++row) {
        const float top = rowTop_[static_cast<std::size_t>(row)], h = rowH_[static_cast<std::size_t>(row)];
        if (ly < top) return -1;  // Luft oder Spalt
        if (ly > top + h) continue;
        const int index = row * m_.columns + col;
        if (index >= count_) return -1;
        const gfx::Rect r = tileRect(index, scroll);
        return y >= r.y && y <= r.y + r.h ? index : -1;
    }
    return -1;
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
