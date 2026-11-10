#include "ui/actions_scene.hpp"

#include "gfx/text.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;

constexpr float kCardMaxW = 420.0f, kPad = 24.0f, kRowH = 52.0f, kRowGap = 8.0f, kTitleH = 44.0f;

const Str kLabels[ActionsScene::kCount] = {Str::VcMapTitle, Str::FetchCovers};
const AppCommand kCommands[ActionsScene::kCount] = {AppCommand::OpenVcMap, AppCommand::FetchCovers};

bool contains(Rect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }
}  // namespace

void ActionsScene::onEnter() {
    focus_ = 0;
    keysSincePointerMove_ = false;
}

Rect ActionsScene::cardRect(const UiContext& ctx) const {
    const float w = ctx.width - 64.0f < kCardMaxW ? ctx.width - 64.0f : kCardMaxW;
    const float h = kPad + kTitleH + kCount * kRowH + (kCount - 1) * kRowGap + kPad;
    return Rect{(ctx.width - w) / 2, (ctx.height - h) / 2, w, h};
}

Rect ActionsScene::rowRect(const UiContext& ctx, int index) const {
    const Rect c = cardRect(ctx);
    return Rect{c.x + kPad, c.y + kPad + kTitleH + static_cast<float>(index) * (kRowH + kRowGap), c.w - 2 * kPad, kRowH};
}

int ActionsScene::hitTest(const UiContext& ctx, float x, float y) const {
    for (int i = 0; i < kCount; ++i)
        if (contains(rowRect(ctx, i), x, y)) return i;
    return -1;
}

void ActionsScene::update(const UiContext& ctx, const InputFrame& in, float, SceneRequests& out) {
    const int hit = in.pointerValid ? hitTest(ctx, in.pointerX, in.pointerY) : -1;
    if (in.pointerValid && in.pointerMoved) {
        if (hit >= 0) focus_ = hit;
        keysSincePointerMove_ = false;
    }
    if (in.pressed(Action::Up) && focus_ > 0) --focus_;
    if (in.pressed(Action::Down) && focus_ + 1 < kCount) ++focus_;
    if (in.pressed(Action::Up) || in.pressed(Action::Down)) keysSincePointerMove_ = true;
    // wie im HOME-Menü: ein Zeiger neben der Liste blockiert A, bis eine Richtungstaste den Fokus übernimmt
    const bool pointerOff = in.pointerValid && hit < 0 && !keysSincePointerMove_;

    if (in.pressed(Action::Back) || in.pressed(Action::Home)) {
        out.pop = true;
        return;
    }
    if (in.pressed(Action::Confirm) && !pointerOff) {
        out.command = kCommands[focus_];
        out.pop = true;
    }
}

void ActionsScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    const Rect card = cardRect(ctx);
    gfx::fillRect(dl, s, Rect{0, 0, ctx.width, ctx.height}, t.overlayDim);
    gfx::roundRect(dl, s, card, 16.0f, t.bg);

    const gfx::FontFace& head = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    gfx::drawTextEllipsis(dl, head, card.x + kPad, card.y + kPad + head.ascent, card.w - 2 * kPad, tr(Str::Settings), t.text);

    const gfx::FontFace& label = ctx.fonts->face(FontSize::Body, FontWeight::Medium);
    for (int i = 0; i < kCount; ++i) {
        const Rect r = rowRect(ctx, i);
        const bool focused = i == focus_;
        if (focused) gfx::roundRect(dl, s, Rect{r.x - 3, r.y - 3, r.w + 6, r.h + 6}, 13, t.accent);
        gfx::roundRect(dl, s, r, 10, focused ? t.tileFocus : t.tile);
        gfx::drawTextEllipsis(dl, label, r.x + 16, r.y + r.h / 2 + (label.ascent - label.descent) / 2, r.w - 32,
                              tr(kLabels[i]), t.text);
    }
}

}  // namespace nl::ui
