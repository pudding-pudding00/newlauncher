#include "ui/details_scene.hpp"

#include <cstdio>

#include "gfx/text.hpp"
#include "ui/layout.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;

constexpr float kMargin = 32.0f, kTop = 32.0f, kCoverW = 200.0f, kCoverH = 280.0f, kInfoBarH = 72.0f;
constexpr float kButtonW = 168.0f, kButtonH = 44.0f, kHintMs = 2000.0f;

bool contains(Rect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

// Tage seit 1970-01-01 → Jahr/Monat/Tag (proleptischer Gregorianischer Kalender, H. Hinnant).
void civilFromDays(long z, int& y, int& m, int& d) {
    z += 719468;
    const long era = (z >= 0 ? z : z - 146096) / 146097;
    const long doe = z - era * 146097;
    const long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y = static_cast<int>(yoe + era * 400 + (m <= 2 ? 1 : 0));
}

void appendf(char* out, std::size_t cap, int& len, const char* fmt, const char* a, unsigned b = 0) {
    if (len < 0 || static_cast<std::size_t>(len) + 1 >= cap) return;
    const int n = std::snprintf(out + len, cap - static_cast<std::size_t>(len), fmt, a, b);
    if (n > 0) len = static_cast<std::size_t>(len + n) >= cap ? static_cast<int>(cap) - 1 : len + n;
}
}  // namespace

int detailsInfoLine(const Game& g, u32 lastPlayedUnix, char* out, std::size_t cap) {
    if (cap == 0) return 0;
    out[0] = '\0';
    int len = 0;
    const ConsoleInfo& c = consoleInfo(g.console);
    appendf(out, cap, len, "%s \xC2\xB7 Gen %u", c.label, c.generation);
    if (g.year != 0) appendf(out, cap, len, "%s%u", " \xC2\xB7 ", g.year);
    if (g.players != 0) {
        appendf(out, cap, len, "%s%u", " \xC2\xB7 ", g.players);
        appendf(out, cap, len, " %s", kStrPlayer);
    }
    if (lastPlayedUnix != 0) {
        int y = 0, m = 0, d = 0;
        civilFromDays(static_cast<long>(lastPlayedUnix / 86400u), y, m, d);
        char date[16];
        std::snprintf(date, sizeof date, "%02d.%02d.%04d", d, m, y);
        appendf(out, cap, len, " \xC2\xB7 %s", kStrLastPlayed);
        appendf(out, cap, len, " %s", date);
    }
    return len;
}

void DetailsScene::open(const Game& game, bool favorite, u32 lastPlayedUnix) {
    key_ = game.key != nullptr ? game.key : "";
    title_ = game.title != nullptr ? game.title : "";
    description_ = game.description != nullptr ? game.description : "";
    game_ = game;
    game_.key = key_.c_str();
    game_.title = title_.c_str();
    game_.description = description_.empty() ? nullptr : description_.c_str();
    game_.path = nullptr;  // für M2 nicht nötig; M3 übergibt den Pfad beim Start
    hasGame_ = true;
    favorite_ = favorite;
    lastPlayed_ = lastPlayedUnix;
    focus_ = 0;
    hintMs_ = 0.0f;
    keysSincePointerMove_ = false;
    detailsInfoLine(game_, lastPlayed_, info_, sizeof info_);
}

Rect DetailsScene::buttonRect([[maybe_unused]] const UiContext& ctx, int index) const {
    const float x = kMargin + kCoverW + 32.0f + static_cast<float>(index) * (kButtonW + 16.0f);
    return Rect{x, kTop + kCoverH - kButtonH, kButtonW, kButtonH};
}

void DetailsScene::update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) {
    if (hintMs_ > 0.0f) hintMs_ -= dtMs;
    if (ctx.minuteOfDay != clockMinute_) {
        clockMinute_ = ctx.minuteOfDay;
        formatClock(clockMinute_, clock_, sizeof clock_);
    }
    int hit = -1;
    if (in.pointerValid) {
        for (int i = 0; i < 2; ++i)
            if (contains(buttonRect(ctx, i), in.pointerX, in.pointerY)) hit = i;
        if (in.pointerMoved) {
            if (hit >= 0) focus_ = hit;
            keysSincePointerMove_ = false;
        }
    }
    if (in.pressed(Action::Left) && focus_ > 0) --focus_;
    if (in.pressed(Action::Right) && focus_ < 1) ++focus_;
    if (in.pressed(Action::Left) || in.pressed(Action::Right)) keysSincePointerMove_ = true;

    if (in.pressed(Action::Back)) {
        out.pop = true;
        return;
    }
    if (in.pressed(Action::Favorite) && hasGame_) {
        out.command = AppCommand::ToggleFavorite;
        out.game = &game_;
        return;
    }
    const bool pointerOff = in.pointerValid && hit < 0 && !keysSincePointerMove_;
    if (in.pressed(Action::Confirm) && !pointerOff && hasGame_) {
        if (focus_ == 0) {
            hintMs_ = kHintMs;  // Starten folgt mit M3
        } else {
            out.command = AppCommand::ToggleFavorite;
            out.game = &game_;
        }
    }
}

void DetailsScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    gfx::fillRect(dl, s, Rect{0, 0, ctx.width, ctx.height}, t.bg);
    if (!hasGame_) return;

    // Cover-Platzhalter (Cover folgen mit M5)
    const Rect cover{kMargin, kTop, kCoverW, kCoverH};
    gfx::roundRect(dl, s, cover, 14, t.tile);
    const gfx::FontFace& big = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    const char* label = consoleInfo(game_.console).label;
    const float lw = gfx::measureText(big, label);
    gfx::drawText(dl, big, cover.x + (cover.w - lw) / 2, cover.y + cover.h / 2, label, t.accentSoftText);

    // Rechte Spalte: Titel (2 Zeilen), Infozeile, Beschreibung (3 Zeilen)
    const float x = cover.x + cover.w + 32.0f;
    const float w = ctx.width - x - kMargin;
    float y = kTop + big.ascent;
    gfx::Line lines[3];
    const std::string_view title(title_);
    int n = gfx::wrapText(big, title, w, std::span<gfx::Line>(lines, 2));
    for (int i = 0; i < n; ++i) {
        gfx::drawTextEllipsis(dl, big, x, y, w, title.substr(lines[i].start, lines[i].length), t.text);
        y += big.lineHeight;
    }
    const gfx::FontFace& body = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    y += 4.0f;
    gfx::drawTextEllipsis(dl, body, x, y + body.ascent, w, info_, t.text2);
    y += body.lineHeight + 12.0f;
    const std::string_view desc(description_);
    n = gfx::wrapText(body, desc, w, lines);
    for (int i = 0; i < n; ++i) {
        gfx::drawTextEllipsis(dl, body, x, y + body.ascent, w, desc.substr(lines[i].start, lines[i].length), t.text);
        y += body.lineHeight;
    }

    // Knöpfe
    const gfx::FontFace& bf = ctx.fonts->face(FontSize::Body, FontWeight::Medium);
    const char* labels[2] = {kStrStart, favorite_ ? kStrFavRemove : kStrFavorite};
    for (int i = 0; i < 2; ++i) {
        const Rect r = buttonRect(ctx, i);
        const bool focused = i == focus_;
        const Color bg = focused ? t.accent : (i == 0 ? t.accentSoft : t.tile);
        if (focused) gfx::roundRect(dl, s, Rect{r.x - 3, r.y - 3, r.w + 6, r.h + 6}, kButtonH / 2 + 3, t.accent);
        gfx::roundRect(dl, s, r, kButtonH / 2, bg);
        const float tw = gfx::measureText(bf, labels[i]);
        gfx::drawTextEllipsis(dl, bf, tw < r.w - 16 ? r.x + (r.w - tw) / 2 : r.x + 8, r.y + r.h / 2 + (bf.ascent - bf.descent) / 2,
                              r.w - 16, labels[i], focused ? t.onAccent : (i == 0 ? t.accentSoftText : t.text));
    }

    // Infoleiste: Tastenhilfe bzw. Hinweis, Uhrzeit
    const float top = ctx.height - kInfoBarH;
    gfx::fillRect(dl, s, Rect{0, top, ctx.width, kInfoBarH}, t.bar);
    gfx::fillRect(dl, s, Rect{0, top, ctx.width, 1}, t.barBorder);
    const char* help = hintMs_ > 0.0f ? kHintStart : kStrDetailsHelp;
    gfx::drawTextEllipsis(dl, body, kMargin, top + 36 + (body.ascent - body.descent) / 2, ctx.width - 2 * kMargin - 80, help,
                          hintMs_ > 0.0f ? t.accentSoftText : t.text2);
    const gfx::FontFace& clock = ctx.fonts->face(FontSize::Body, FontWeight::Medium);
    const float cw = gfx::measureText(clock, clock_);
    gfx::drawText(dl, clock, ctx.width - kMargin - cw, top + 36 + (clock.ascent - clock.descent) / 2, clock_, t.text2);
}

}  // namespace nl::ui
