#include "ui/home_menu.hpp"

#include <cstdio>

#include "gfx/text.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;

constexpr float kHeaderH = 64.0f, kFooterH = 56.0f;
constexpr float kTileH = 150.0f, kTileGap = 16.0f, kMaxTileW = 150.0f;

const Str kLabels[HomeMenu::kTileCount] = {Str::HomebrewChannel, Str::WiiMenu, Str::Settings, Str::PowerOff,
                                           Str::ReadDisc};
const AppCommand kCommands[HomeMenu::kTileCount] = {AppCommand::ExitToHbc, AppCommand::WiiMenu, AppCommand::None,
                                                    AppCommand::PowerOff, AppCommand::ReadDisc};

bool contains(Rect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }
}  // namespace

void HomeMenu::onEnter() {
    focus_ = 0;
    keysSincePointerMove_ = false;
}

Rect HomeMenu::tileRect(const UiContext& ctx, int index) const {
    float w = (ctx.width - 80.0f - static_cast<float>(kTileCount - 1) * kTileGap) / kTileCount;
    if (w > kMaxTileW) w = kMaxTileW;
    const float total = kTileCount * w + (kTileCount - 1) * kTileGap;
    const float x0 = (ctx.width - total) / 2.0f;
    const float y = (ctx.height - kTileH) / 2.0f;
    return Rect{x0 + static_cast<float>(index) * (w + kTileGap), y, w, kTileH};
}

int HomeMenu::hitTest(const UiContext& ctx, float x, float y) const {
    for (int i = 0; i < kTileCount; ++i)
        if (contains(tileRect(ctx, i), x, y)) return i;
    return -1;
}

void HomeMenu::update(const UiContext& ctx, const InputFrame& in, float, SceneRequests& out) {
    FocusSound sound(out, focus_);
    const int hit = in.pointerValid ? hitTest(ctx, in.pointerX, in.pointerY) : -1;
    if (in.pointerValid && in.pointerMoved) {
        if (hit >= 0) focus_ = hit;
        keysSincePointerMove_ = false;
    }
    if (in.pressed(Action::Left) && focus_ > 0) --focus_;
    if (in.pressed(Action::Right) && focus_ + 1 < kTileCount) ++focus_;
    if (in.pressed(Action::Left) || in.pressed(Action::Right)) keysSincePointerMove_ = true;
    // Ein gültiger Zeiger neben den Kacheln blockiert Bestätigen, solange keine Richtungstaste
    // den Fokus übernommen hat; die Lage wird jeden Frame aus der aktuellen Position bestimmt.
    const bool pointerOffTiles = in.pointerValid && hit < 0 && !keysSincePointerMove_;

    if (in.pressed(Action::Home) || in.pressed(Action::Back)) {
        out.pop = true;
        return;
    }
    if (in.pressed(Action::Confirm) && !pointerOffTiles) {
        if (focus_ == 2) {  // Einstellungen: HOME ausblenden, dann die Einstellungsszene öffnen
            if (settings_ != nullptr) {
                out.pop = true;
                out.push = settings_;
            }
            return;
        }
        if (focus_ == 4) out.pop = true;
        out.command = kCommands[focus_];
    }
}

void HomeMenu::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;

    gfx::fillRect(dl, s, Rect{0, 0, ctx.width, kHeaderH}, t.bar);
    gfx::fillRect(dl, s, Rect{0, kHeaderH, ctx.width, 3}, t.accent);
    const gfx::FontFace& head = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    gfx::drawText(dl, head, 32, kHeaderH / 2 + (head.ascent - head.descent) / 2, tr(Str::HomeMenu), t.text);

    const gfx::FontFace& label = ctx.fonts->face(FontSize::Body, FontWeight::Medium);
    for (int i = 0; i < kTileCount; ++i) {
        const Rect r = tileRect(ctx, i);
        const bool focused = i == focus_;
        if (focused) gfx::roundRect(dl, s, Rect{r.x - 3, r.y - 3, r.w + 6, r.h + 6}, 17, t.accent);
        gfx::roundRect(dl, s, r, 14, focused ? t.tileFocus : t.tile);
        gfx::circle(dl, s, r.x + r.w / 2, r.y + 52, 26, t.accentSoft);

        gfx::Line lines[2];
        const std::string_view text(tr(kLabels[i]));
        const int n = gfx::wrapText(label, text, r.w - 16, lines);
        float baseline = r.y + 104 + label.ascent / 2;
        for (int k = 0; k < n; ++k) {
            const std::string_view line = text.substr(lines[k].start, lines[k].length);
            const float w = gfx::measureText(label, line);
            const float x = w < r.w - 16 ? r.x + (r.w - w) / 2 : r.x + 8;
            gfx::drawTextEllipsis(dl, label, x, baseline, r.w - 16, line, t.text);
            baseline += label.lineHeight;
        }
    }

    gfx::fillRect(dl, s, Rect{0, ctx.height - kFooterH, ctx.width, kFooterH}, t.bar);
    const gfx::FontFace& small = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    const float base = ctx.height - kFooterH / 2 + (small.ascent - small.descent) / 2;
    // links P1..P4 mit vier Balken; schwach (<= 1 Balken) in danger, nicht verbunden als „P<n> –“
    constexpr float kBarW = 5.0f, kBarGap = 2.0f, kBarH = 12.0f, kGroupGap = 18.0f;
    float x = 32.0f;
    for (int p = 0; p < 4; ++p) {
        const int bars = ctx.battery[static_cast<std::size_t>(p)];
        char name[8];
        std::snprintf(name, sizeof name, bars < 0 ? "P%d \xE2\x80\x93" : "P%d", p + 1);
        gfx::drawText(dl, small, x, base, name, bars < 0 ? t.textMuted : t.text2);
        x += gfx::measureText(small, name) + 6.0f;
        if (bars >= 0) {
            const Color on = bars <= 1 ? t.danger : t.text2;
            const float top = ctx.height - kFooterH / 2 - kBarH / 2;
            for (int b = 0; b < 4; ++b) {
                gfx::fillRect(dl, s, Rect{x, top, kBarW, kBarH}, b < bars ? on : t.barBorder);
                x += kBarW + kBarGap;
            }
        }
        x += kGroupGap;
    }
    const float w = gfx::measureText(small, tr(Str::HomeCloses));
    gfx::drawText(dl, small, ctx.width - 32 - w, base, tr(Str::HomeCloses), t.text2);
}

}  // namespace nl::ui
