#include "ui/pin_scene.hpp"

#include "gfx/text.hpp"

namespace nl::ui {

namespace {
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;
constexpr float kCardW = 360.0f, kCardH = 220.0f, kBox = 44.0f, kGap = 14.0f;
}  // namespace

void PinScene::clear() {
    digits_ = {'0', '0', '0', '0'};
    cursor_ = 0;
}

void PinScene::open(PinMode mode) {
    mode_ = mode;
    repeat_ = false;
    first_ = {};
    hasMessage_ = false;
    closeNext_ = false;
    next_ = nullptr;
    home_ = nullptr;
    clear();
}

void PinScene::reject(Str message) {
    clear();
    message_ = message;
    hasMessage_ = true;
}

void PinScene::update(const UiContext&, const InputFrame& in, float, SceneRequests& out) {
    FocusSound sound(out, cursor_);
    if (closeNext_) {
        closeNext_ = false;
        sound.silence();
        out.pop = true;
        out.push = next_;
        next_ = nullptr;
        return;
    }
    if (in.pressed(Action::Back) || in.pressed(Action::Home)) {
        out.pop = true;
        if (in.pressed(Action::Home)) out.push = home_;  // HOME heißt überall HOME-Menü, auch aus der Abfrage
        return;
    }
    char& d = digits_[static_cast<std::size_t>(cursor_)];
    if (in.pressed(Action::Up)) {
        d = d == '9' ? '0' : static_cast<char>(d + 1);
        out.sound = UiSound::Move;
    }
    if (in.pressed(Action::Down)) {
        d = d == '0' ? '9' : static_cast<char>(d - 1);
        out.sound = UiSound::Move;
    }
    if (in.pressed(Action::Left) && cursor_ > 0) --cursor_;
    if (in.pressed(Action::Right) && cursor_ < 3) ++cursor_;
    if (!in.pressed(Action::Confirm)) return;
    if (cursor_ < 3) {
        ++cursor_;
        return;
    }
    if (mode_ == PinMode::Set && !repeat_) {
        first_ = digits_;
        repeat_ = true;
        clear();
        message_ = Str::PinRepeat;
        hasMessage_ = true;
        out.sound = UiSound::Confirm;
        return;
    }
    if (mode_ == PinMode::Set && digits_ != first_) {
        repeat_ = false;
        reject(Str::PinMismatch);
        out.sound = UiSound::Error;
        return;
    }
    out.command = AppCommand::PinEntered;
}

Rect PinScene::cardRect(const UiContext& ctx) const {
    const float w = ctx.width - 32.0f < kCardW ? ctx.width - 32.0f : kCardW;
    return Rect{(ctx.width - w) / 2, (ctx.height - kCardH) / 2, w, kCardH};
}

void PinScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    const Rect card = cardRect(ctx);
    gfx::fillRect(dl, s, Rect{0, 0, ctx.width, ctx.height}, t.overlayDim);
    gfx::roundRect(dl, s, card, 16.0f, t.bg);

    const gfx::FontFace& head = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    const Str title = mode_ == PinMode::Verify ? Str::PinEnter : repeat_ ? Str::PinRepeat : Str::PinNew;
    gfx::drawTextEllipsis(dl, head, card.x + 16, card.y + 16 + head.ascent, card.w - 32, tr(title), t.text);

    const gfx::FontFace& big = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    const float rowW = 4 * kBox + 3 * kGap;
    const float y = card.y + 72;
    for (int i = 0; i < 4; ++i) {
        const Rect box{card.x + (card.w - rowW) / 2 + static_cast<float>(i) * (kBox + kGap), y, kBox, kBox};
        gfx::roundRect(dl, s, box, 8.0f, i == cursor_ ? t.tileFocus : t.tile);
        // Nur die Stelle unter dem Cursor zeigt ihre Ziffer; die anderen bleiben verdeckt.
        const char glyph[2] = {i == cursor_ ? digits_[static_cast<std::size_t>(i)] : '*', '\0'};
        const float gw = gfx::measureText(big, glyph);
        gfx::drawText(dl, big, box.x + (kBox - gw) / 2, box.y + kBox / 2 + (big.ascent - big.descent) / 2, glyph, t.text);
    }

    const gfx::FontFace& body = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    if (hasMessage_) {
        const char* m = tr(message_);
        const float mw = gfx::measureText(body, m);
        const float mx = mw < card.w - 32 ? card.x + (card.w - mw) / 2 : card.x + 16;
        gfx::drawTextEllipsis(dl, body, mx, y + kBox + 32, card.w - 32, m, t.accent);
    }
    const gfx::FontFace& small = ctx.fonts->face(FontSize::Small, FontWeight::Regular);
    gfx::drawTextEllipsis(dl, small, card.x + 16, card.y + card.h - 16 - small.descent, card.w - 32, tr(Str::PinHelp),
                          t.textMuted);
}

}  // namespace nl::ui
