#include "ui/jump_bar.hpp"

#include "gfx/text.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
using gfx::Rect;

constexpr float kMargin = 16.0f, kCellH = 44.0f, kSearchW = 84.0f, kGap = 8.0f, kInfoBarH = 72.0f;

char bucketOf(int cell) { return cell == 0 ? '#' : static_cast<char>('A' + cell - 1); }

bool contains(Rect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }
}  // namespace

void JumpBar::onEnter() {
    const char b = target_.currentBucket();
    focus_ = (b >= 'A' && b <= 'Z') ? b - 'A' + 1 : 0;
    keysSincePointerMove_ = false;
}

Rect JumpBar::cellRect(const UiContext& ctx, int index) const {
    const float y = ctx.height - kInfoBarH - kCellH - 12.0f;
    const float letterW = (ctx.width - 2 * kMargin - kSearchW - kGap) / (kCellCount - 1);
    if (index == kSearchCell) return Rect{ctx.width - kMargin - kSearchW, y, kSearchW, kCellH};
    return Rect{kMargin + static_cast<float>(index) * letterW, y, letterW, kCellH};
}

void JumpBar::update(const UiContext& ctx, const InputFrame& in, float, SceneRequests& out) {
    int hit = -1;
    if (in.pointerValid) {
        for (int i = 0; i < kCellCount && hit < 0; ++i)
            if (contains(cellRect(ctx, i), in.pointerX, in.pointerY)) hit = i;
        if (in.pointerMoved) {
            if (hit >= 0) focus_ = hit;
            keysSincePointerMove_ = false;
        }
    }
    if (in.pressed(Action::Left) && focus_ > 0) --focus_;
    if (in.pressed(Action::Right) && focus_ < kCellCount - 1) ++focus_;
    if (in.pressed(Action::Left) || in.pressed(Action::Right)) keysSincePointerMove_ = true;

    if (in.pressed(Action::Back) || in.pressed(Action::Search) || in.pressed(Action::Home)) {
        out.pop = true;
        return;
    }
    const bool pointerOff = in.pointerValid && hit < 0 && !keysSincePointerMove_;
    if (in.pressed(Action::Confirm) && !pointerOff) {
        if (focus_ == kSearchCell) out.push = target_.openSearch();
        else target_.jumpToBucket(bucketOf(focus_));
        out.pop = true;
    }
}

void JumpBar::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    const Rect first = cellRect(ctx, 0);
    gfx::roundRect(dl, s, Rect{kMargin - 6, first.y - 6, ctx.width - 2 * kMargin + 12, kCellH + 12}, 12, t.bar);
    const gfx::FontFace& f = ctx.fonts->face(gfx::FontSize::Body, gfx::FontWeight::Medium);
    for (int i = 0; i < kCellCount; ++i) {
        const Rect r = cellRect(ctx, i);
        const bool focused = i == focus_;
        if (focused) gfx::roundRect(dl, s, Rect{r.x + 1, r.y + 4, r.w - 2, r.h - 8}, 8, t.accent);
        char letter[2] = {bucketOf(i), '\0'};
        const char* label = i == kSearchCell ? tr(Str::Search) : letter;
        const bool available = i == kSearchCell || target_.hasBucket(bucketOf(i));
        const Color c = focused ? t.onAccent : (available ? t.text : t.textMuted);
        const float w = gfx::measureText(f, label);
        gfx::drawTextEllipsis(dl, f, r.x + (r.w - w) / 2 > r.x ? r.x + (r.w - w) / 2 : r.x, r.y + r.h / 2 + (f.ascent - f.descent) / 2,
                              r.w, label, c);
    }
}

}  // namespace nl::ui
