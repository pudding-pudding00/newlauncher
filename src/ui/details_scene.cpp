#include "ui/details_scene.hpp"

#include <algorithm>
#include <cstdio>

#include "core/library_merge.hpp"
#include "gfx/text.hpp"
#include "ui/launch_text.hpp"
#include "ui/layout.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;

constexpr float kMargin = 32.0f, kTop = 32.0f, kCoverW = 200.0f, kCoverH = 280.0f, kInfoBarH = 72.0f;
constexpr float kButtonW = 168.0f, kButtonH = 44.0f;
constexpr float kMethodH = 40.0f, kMethodGap = 16.0f;
constexpr int kChoiceCount = 1 + static_cast<int>(kN64MethodCount);

constexpr N64Method methodOf(int choice) { return static_cast<N64Method>(choice - 1); }

const char* whyText(N64Unavailable why) {
    switch (why) {
        case N64Unavailable::NotMapped: return tr(Str::WhyNotMapped);
        case N64Unavailable::NotInstalled: return tr(Str::WhyNotInstalled);
        case N64Unavailable::DolMissing: return tr(Str::WhyDolMissing);
        case N64Unavailable::None: break;
    }
    return "";
}

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

std::string detailsDeviceLine(std::span<const std::string> devices, u8 device, std::span<const u8> alsoOn) {
    if (devices.size() < 2 || device >= devices.size()) return {};
    const auto fill = [](const char* fmt, const std::string& v) {
        std::string s(fmt);
        const std::size_t at = s.find("%s");
        if (at != std::string::npos) s.replace(at, 2, v);
        return s;
    };
    std::string line = fill(tr(Str::DeviceLine), deviceLabel(devices[device]));
    std::string others;
    for (u8 d : alsoOn) {
        if (d >= devices.size()) continue;
        if (!others.empty()) others += ", ";
        others += deviceLabel(devices[d]);
    }
    if (!others.empty()) line += " (" + fill(tr(Str::AlsoOn), others) + ")";
    return line;
}

int detailsInfoLine(const Game& g, u32 lastPlayedUnix, char* out, std::size_t cap) {
    if (cap == 0) return 0;
    out[0] = '\0';
    int len = 0;
    const ConsoleInfo& c = consoleInfo(g.console);
    appendf(out, cap, len, "%s \xC2\xB7 Gen %u", c.label, c.generation);
    if (g.year != 0) appendf(out, cap, len, "%s%u", " \xC2\xB7 ", g.year);
    if (g.players != 0) {
        appendf(out, cap, len, "%s%u", " \xC2\xB7 ", g.players);
        appendf(out, cap, len, " %s", tr(Str::Player));
    }
    if (lastPlayedUnix != 0) {
        int y = 0, m = 0, d = 0;
        civilFromDays(static_cast<long>(lastPlayedUnix / 86400u), y, m, d);
        char date[32];
        std::snprintf(date, sizeof date, "%02d.%02d.%04d", d, m, y);
        appendf(out, cap, len, " \xC2\xB7 %s", tr(Str::LastPlayed));
        appendf(out, cap, len, " %s", date);
    }
    return len;
}

int n64HintLine(const N64Decision& d, char* out, std::size_t cap) {
    if (cap == 0) return 0;
    std::string s;
    if (d.overrideIgnored || d.mappingIgnored) s = tr(Str::OverrideIgnored);
    bool first = true;
    for (int i = 0; i < static_cast<int>(kN64MethodCount); ++i) {
        const N64Unavailable why = d.why[static_cast<std::size_t>(i)];
        if (why == N64Unavailable::None) continue;
        if (!first) s += " \xC2\xB7 ";
        else if (!s.empty()) s += ' ';
        first = false;
        s += n64MethodLabel(static_cast<N64Method>(i));
        s += ": ";
        s += whyText(why);
    }
    return static_cast<int>(copyEllipsized(out, cap, s));
}

void DetailsScene::setN64(const N64Decision& decision, std::optional<N64Method> override_) {
    if (!hasGame_ || game_.console != Console::N64) return;
    hasN64_ = true;
    n64_ = decision;
    choice_ = override_ ? 1 + static_cast<int>(*override_) : 0;
    n64HintLine(n64_, hint_, sizeof hint_);
    refreshN64Text();
}

bool DetailsScene::choiceAllowed(int c) const { return c == 0 || n64_.available(methodOf(c)); }

void DetailsScene::refreshN64Text() {
    char value[80];
    if (choice_ != 0) {
        std::snprintf(value, sizeof value, "%s", n64MethodLabel(methodOf(choice_)));
    } else {
        const char* autoLabel = nullptr;
        for (int c = 1; c < kChoiceCount && autoLabel == nullptr; ++c)
            if (n64_.available(methodOf(c))) autoLabel = n64MethodLabel(methodOf(c));
        if (autoLabel != nullptr) std::snprintf(value, sizeof value, tr(Str::MethodAuto), autoLabel);
        else std::snprintf(value, sizeof value, "%s", tr(Str::MethodAutoNone));
    }
    std::snprintf(method_, sizeof method_, tr(Str::Method), value);
}

void DetailsScene::open(const Game& game, bool favorite, u32 lastPlayedUnix) {
    key_ = game.key != nullptr ? game.key : "";
    title_ = game.title != nullptr ? game.title : "";
    description_ = game.description != nullptr ? game.description : "";
    path_ = game.path != nullptr ? game.path : "";
    game_ = game;
    game_.key = key_.c_str();
    game_.title = title_.c_str();
    game_.description = description_.empty() ? nullptr : description_.c_str();
    game_.path = path_.c_str();
    hasGame_ = true;
    device_.clear();
    favorite_ = favorite;
    lastPlayed_ = lastPlayedUnix;
    focus_ = 0;
    keysSincePointerMove_ = false;
    hasN64_ = false;
    n64_ = N64Decision{};
    choice_ = 0;
    method_[0] = '\0';
    hint_[0] = '\0';
    detailsInfoLine(game_, lastPlayed_, info_, sizeof info_);
}

Rect DetailsScene::buttonRect([[maybe_unused]] const UiContext& ctx, int index) const {
    const float x = kMargin + kCoverW + 32.0f + static_cast<float>(index) * (kButtonW + 16.0f);
    return Rect{x, kTop + kCoverH - kButtonH, kButtonW, kButtonH};
}

void DetailsScene::update(const UiContext& ctx, const InputFrame& in, float, SceneRequests& out) {
    FocusSound sound(out, focus_);
    if (covers_ != nullptr && hasGame_) covers_->want(game_, CoverPriority::Visible);
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
    if (focus_ == kFocusMethod) {
        const int dir = in.pressed(Action::Right) ? 1 : in.pressed(Action::Left) ? -1 : 0;
        if (dir != 0) {
            keysSincePointerMove_ = true;
            int c = choice_;
            do c = (c + dir + kChoiceCount) % kChoiceCount;
            while (!choiceAllowed(c));
            choice_ = c;
            refreshN64Text();
            out.command = AppCommand::SetN64Method;
            out.game = &game_;
            out.n64Choice = static_cast<u8>(choice_);
            return;
        }
        if (in.pressed(Action::Down)) {
            focus_ = 0;
            keysSincePointerMove_ = true;
        }
    } else {
        if (in.pressed(Action::Left) && focus_ > 0) --focus_;
        if (in.pressed(Action::Right) && focus_ < 1) ++focus_;
        if (in.pressed(Action::Up) && hasN64_) focus_ = kFocusMethod;
        if (in.pressed(Action::Left) || in.pressed(Action::Right) || in.pressed(Action::Up)) keysSincePointerMove_ = true;
    }

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
    if (in.pressed(Action::Confirm) && !pointerOff && hasGame_ && focus_ != kFocusMethod) {
        out.command = focus_ == 0 ? AppCommand::Launch : AppCommand::ToggleFavorite;
        out.game = &game_;
    }
}

void DetailsScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    gfx::fillRect(dl, s, Rect{0, 0, ctx.width, ctx.height}, t.bg);
    if (!hasGame_) return;

    // Cover über dem Platzhalter (Konsolenname), eingeblendet mit dem Alpha des Providers
    const Rect cover{kMargin, kTop, kCoverW, kCoverH};
    gfx::roundRect(dl, s, cover, 14, t.tile);
    const gfx::FontFace& big = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    const char* label = consoleInfo(game_.console).label;
    const float lw = gfx::measureText(big, label);
    gfx::drawText(dl, big, cover.x + (cover.w - lw) / 2, cover.y + cover.h / 2, label, t.accentSoftText);
    if (covers_ != nullptr) {
        const CoverView cv = covers_->view(game_);
        if (cv.texture != 0 && cv.alpha > 0.0f) {
            const u8 a = static_cast<u8>(std::min(cv.alpha, 1.0f) * 255.0f);
            dl.quad(cv.texture, cover, gfx::UvRect{0, 0, 1, 1}, Color{255, 255, 255, a});
        }
    }

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
    y += body.lineHeight;
    if (!device_.empty()) {
        gfx::drawTextEllipsis(dl, body, x, y + body.ascent, w, device_, t.text2);
        y += body.lineHeight;
    }
    y += 12.0f;
    const std::string_view desc(description_);
    const int descLines = (hasN64_ ? 2 : 3) - (device_.empty() ? 0 : 1);
    n = gfx::wrapText(body, desc, w, std::span<gfx::Line>(lines, static_cast<std::size_t>(descLines)));
    for (int i = 0; i < n; ++i) {
        gfx::drawTextEllipsis(dl, body, x, y + body.ascent, w, desc.substr(lines[i].start, lines[i].length), t.text);
        y += body.lineHeight;
    }

    // N64: Startmethode zwischen Beschreibung und Knöpfen, Gründe unter den Knöpfen
    if (hasN64_) {
        const Rect b0 = buttonRect(ctx, 0);
        const Rect row{b0.x, b0.y - kMethodGap - kMethodH, 2 * kButtonW + 16.0f, kMethodH};
        const bool focused = focus_ == kFocusMethod;
        if (focused) gfx::roundRect(dl, s, Rect{row.x - 3, row.y - 3, row.w + 6, row.h + 6}, kMethodH / 2 + 3, t.accent);
        gfx::roundRect(dl, s, row, kMethodH / 2, t.tile);
        gfx::drawTextEllipsis(dl, body, row.x + 16, row.y + row.h / 2 + (body.ascent - body.descent) / 2, row.w - 32, method_, t.text);
        if (hint_[0] != '\0')
            gfx::drawTextEllipsis(dl, body, b0.x, b0.y + kButtonH + 12 + body.ascent, w, hint_, t.text2);
    }

    // Knöpfe
    const gfx::FontFace& bf = ctx.fonts->face(FontSize::Body, FontWeight::Medium);
    const char* labels[2] = {tr(Str::Start), favorite_ ? tr(Str::FavRemove) : tr(Str::Favorite)};
    for (int i = 0; i < 2; ++i) {
        const Rect r = buttonRect(ctx, i);
        const bool focused = i == focus_;  // kFocusMethod hebt keinen Knopf hervor
        const Color bg = focused ? t.accent : (i == 0 ? t.accentSoft : t.tile);
        if (focused) gfx::roundRect(dl, s, Rect{r.x - 3, r.y - 3, r.w + 6, r.h + 6}, kButtonH / 2 + 3, t.accent);
        gfx::roundRect(dl, s, r, kButtonH / 2, bg);
        const float tw = gfx::measureText(bf, labels[i]);
        gfx::drawTextEllipsis(dl, bf, tw < r.w - 16 ? r.x + (r.w - tw) / 2 : r.x + 8, r.y + r.h / 2 + (bf.ascent - bf.descent) / 2,
                              r.w - 16, labels[i], focused ? t.onAccent : (i == 0 ? t.accentSoftText : t.text));
    }

    // Infoleiste: Tastenhilfe, Uhrzeit
    const float top = ctx.height - kInfoBarH;
    gfx::fillRect(dl, s, Rect{0, top, ctx.width, kInfoBarH}, t.bar);
    gfx::fillRect(dl, s, Rect{0, top, ctx.width, 1}, t.barBorder);
    gfx::drawTextEllipsis(dl, body, kMargin, top + 36 + (body.ascent - body.descent) / 2, ctx.width - 2 * kMargin - 80,
                          tr(Str::DetailsHelp), t.text2);
    const gfx::FontFace& clock = ctx.fonts->face(FontSize::Body, FontWeight::Medium);
    const float cw = gfx::measureText(clock, clock_);
    gfx::drawText(dl, clock, ctx.width - kMargin - cw, top + 36 + (clock.ascent - clock.descent) / 2, clock_, t.text2);
}

}  // namespace nl::ui
