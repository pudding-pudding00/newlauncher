#include "ui/jump_bar.hpp"

#include "gfx/text.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
using gfx::Rect;

constexpr float kMargin = 16.0f, kCellH = 44.0f, kSearchW = 84.0f, kGap = 8.0f, kInfoBarH = 72.0f, kFilterH = 36.0f;

char bucketOf(int cell) { return cell == 0 ? '#' : static_cast<char>('A' + cell - 1); }

bool contains(Rect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }
}  // namespace

void JumpBar::onEnter() {
    const char b = target_.currentBucket();
    focus_ = (b >= 'A' && b <= 'Z') ? b - 'A' + 1 : 0;
    filterRow_ = false;
    keysSincePointerMove_ = false;
}

Rect JumpBar::cellRect(const UiContext& ctx, int index) const {
    const float y = ctx.height - kInfoBarH - kCellH - 12.0f;
    const float letterW = (ctx.width - 2 * kMargin - kSearchW - kGap) / (kCellCount - 1);
    if (index == kSearchCell) return Rect{ctx.width - kMargin - kSearchW, y, kSearchW, kCellH};
    return Rect{kMargin + static_cast<float>(index) * letterW, y, letterW, kCellH};
}

Rect JumpBar::filterRect(const UiContext& ctx) const {
    const Rect first = cellRect(ctx, 0);
    const float w = ctx.width / 2;
    return Rect{(ctx.width - w) / 2, first.y - 6 - 12 - kFilterH, w, kFilterH};
}

void JumpBar::stepFilter(int dir, SceneRequests& out) {
    const int n = target_.filterCount();
    if (n <= 0) return;
    target_.setFilterIndex(stepFilterIndex(target_.filterIndex(), dir, n));
    out.sound = UiSound::Move;
}

void JumpBar::update(const UiContext& ctx, const InputFrame& in, float, SceneRequests& out) {
    FocusSound sound(out, focus_);
    const bool hasFilter = target_.filterCount() > 0;
    if (!hasFilter) filterRow_ = false;
    int hit = -1;
    bool hitFilter = false;
    if (in.pointerValid) {
        for (int i = 0; i < kCellCount && hit < 0; ++i)
            if (contains(cellRect(ctx, i), in.pointerX, in.pointerY)) hit = i;
        hitFilter = hasFilter && contains(filterRect(ctx), in.pointerX, in.pointerY);
        if (in.pointerMoved) {
            if (hit >= 0) {
                focus_ = hit;
                filterRow_ = false;
            } else if (hitFilter) {
                filterRow_ = true;
            }
            keysSincePointerMove_ = false;
        }
    }
    if (in.pressed(Action::Up) && hasFilter && !filterRow_) {
        filterRow_ = true;
        out.sound = UiSound::Move;
    }
    if (in.pressed(Action::Down) && filterRow_) {
        filterRow_ = false;
        out.sound = UiSound::Move;
    }
    if (filterRow_) {
        if (in.pressed(Action::Left)) stepFilter(-1, out);
        if (in.pressed(Action::Right)) stepFilter(1, out);
    } else {
        if (in.pressed(Action::Left) && focus_ > 0) --focus_;
        if (in.pressed(Action::Right) && focus_ < kCellCount - 1) ++focus_;
    }
    if (in.pressed(Action::Left) || in.pressed(Action::Right) || in.pressed(Action::Up) || in.pressed(Action::Down))
        keysSincePointerMove_ = true;

    if (in.pressed(Action::Back) || in.pressed(Action::Search) || in.pressed(Action::Home)) {
        out.pop = true;
        return;
    }
    const bool pointerOff = in.pointerValid && hit < 0 && !hitFilter && !keysSincePointerMove_;
    if (in.pressed(Action::Confirm) && !pointerOff) {
        if (filterRow_) {
            stepFilter(1, out);  // A blättert weiter; die Leiste bleibt offen
            return;
        }
        if (focus_ == kSearchCell) out.push = target_.openSearch();
        else target_.jumpToBucket(bucketOf(focus_));
        out.pop = true;
        out.sound = out.push != nullptr ? UiSound::Open : UiSound::Confirm;
    }
}

void JumpBar::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    const Rect first = cellRect(ctx, 0);
    gfx::roundRect(dl, s, Rect{kMargin - 6, first.y - 6, ctx.width - 2 * kMargin + 12, kCellH + 12}, 12, t.bar);
    const gfx::FontFace& f = ctx.fonts->face(gfx::FontSize::Body, gfx::FontWeight::Medium);
    if (target_.filterCount() > 0) {
        const Rect r = filterRect(ctx);
        gfx::roundRect(dl, s, r, 10, filterRow_ ? t.accent : t.bar);
        const std::string label =
            std::string(tr(Str::Filter)) + ":  < " + target_.filterLabel(target_.filterIndex()) + " >";
        const float w = gfx::measureText(f, label.c_str());
        const float x = w < r.w - 16 ? r.x + (r.w - w) / 2 : r.x + 8;
        gfx::drawTextEllipsis(dl, f, x, r.y + r.h / 2 + (f.ascent - f.descent) / 2, r.w - 16, label.c_str(),
                              filterRow_ ? t.onAccent : t.text);
    }
    for (int i = 0; i < kCellCount; ++i) {
        const Rect r = cellRect(ctx, i);
        const bool focused = i == focus_ && !filterRow_;
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
