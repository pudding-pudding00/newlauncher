#include "ui/choice_dialog.hpp"

#include <string_view>

#include "gfx/text.hpp"
#include "ui/launch_text.hpp"

namespace nl::ui {

namespace {
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;

constexpr float kCardMaxW2 = 440.0f, kCardMaxW3 = 560.0f, kPad = 24.0f, kButtonH = 44.0f, kGap = 20.0f,
                kButtonGap = 12.0f;

bool contains(Rect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }
}  // namespace

struct ChoiceDialog::Layout {
    Rect card;
    Rect buttons[kMaxButtons];
    float textX, textW, titleBaseline, textTop;
    gfx::Line lines[kMaxLines];
    u16 offsets[kMaxLines];  // Anfang des Absatzes, auf den sich lines[i] bezieht
    int count;
};

void ChoiceDialog::open(const char* title, const char* text, std::span<const char* const> buttons, AppCommand onPick,
                        int defaultIndex) {
    copyEllipsized(title_, sizeof title_, title != nullptr ? title : "");
    copyEllipsized(text_, sizeof text_, text != nullptr ? text : "");
    count_ = 0;
    for (const char* b : buttons) {
        if (count_ == kMaxButtons) break;
        copyEllipsized(labels_[count_], sizeof labels_[count_], b != nullptr ? b : "");
        ++count_;
    }
    focus_ = defaultIndex >= 0 && defaultIndex < count_ ? defaultIndex : 0;
    picked_ = -1;
    onPick_ = onPick;
}

ChoiceDialog::Layout ChoiceDialog::layout(const UiContext& ctx) const {
    Layout l{};
    const gfx::FontFace& head = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    const gfx::FontFace& body = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    const float maxW = count_ > 2 ? kCardMaxW3 : kCardMaxW2;
    const float cardW = ctx.width - 64.0f < maxW ? ctx.width - 64.0f : maxW;
    l.textW = cardW - 2 * kPad;

    // Absätze an '\n' trennen, jeden für sich umbrechen (wie DialogScene).
    const std::string_view all(text_);
    std::size_t start = 0;
    while (l.count < kMaxLines) {
        const std::size_t nl = all.find('\n', start);
        const std::size_t end = nl == std::string_view::npos ? all.size() : nl;
        const std::string_view para = all.substr(start, end - start);
        if (para.empty()) {
            l.lines[l.count] = gfx::Line{0, 0};
            l.offsets[l.count++] = static_cast<u16>(start);
        } else {
            const int n = gfx::wrapText(body, para, l.textW,
                                        std::span<gfx::Line>(l.lines + l.count, static_cast<std::size_t>(kMaxLines - l.count)));
            for (int i = 0; i < n; ++i) l.offsets[l.count + i] = static_cast<u16>(start);
            l.count += n;
        }
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }

    const float cardH =
        kPad + head.lineHeight + 8.0f + static_cast<float>(l.count) * body.lineHeight + kGap + kButtonH + kPad;
    l.card = Rect{(ctx.width - cardW) / 2, (ctx.height - cardH) / 2, cardW, cardH};
    l.textX = l.card.x + kPad;
    l.titleBaseline = l.card.y + kPad + head.ascent;
    l.textTop = l.card.y + kPad + head.lineHeight + 8.0f;
    const int n = count_ > 0 ? count_ : 1;
    const float bw = (l.textW - static_cast<float>(n - 1) * kButtonGap) / static_cast<float>(n);
    const float by = l.card.y + cardH - kPad - kButtonH;
    for (int i = 0; i < count_; ++i)
        l.buttons[i] = Rect{l.textX + static_cast<float>(i) * (bw + kButtonGap), by, bw, kButtonH};
    return l;
}

Rect ChoiceDialog::buttonRect(const UiContext& ctx, int index) const {
    if (index < 0 || index >= count_) return Rect{0, 0, 0, 0};
    return layout(ctx).buttons[index];
}

void ChoiceDialog::update(const UiContext& ctx, const InputFrame& in, float, SceneRequests& out) {
    auto finish = [&](int choice, UiSound sound) {
        picked_ = choice;
        out.pop = true;
        out.command = onPick_;
        out.sound = sound;
    };
    if (in.pressed(Action::Back)) return finish(-1, UiSound::Back);
    if (in.pressed(Action::Left) && focus_ > 0) {
        --focus_;
        out.sound = UiSound::Move;
    }
    if (in.pressed(Action::Right) && focus_ + 1 < count_) {
        ++focus_;
        out.sound = UiSound::Move;
    }
    if (in.pointerValid && in.pointerMoved) {
        const Layout l = layout(ctx);
        for (int i = 0; i < count_; ++i)
            if (contains(l.buttons[i], in.pointerX, in.pointerY)) focus_ = i;
    }
    if (in.pressed(Action::Confirm) && count_ > 0) {
        // Mit sichtbarem Zeiger zählt A nur auf einem Knopf, ohne Zeiger wählt A den Fokus.
        if (!in.pointerValid) return finish(focus_, UiSound::Confirm);
        const Layout l = layout(ctx);
        for (int i = 0; i < count_; ++i)
            if (contains(l.buttons[i], in.pointerX, in.pointerY)) return finish(i, UiSound::Confirm);
    }
}

void ChoiceDialog::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    const Layout l = layout(ctx);
    gfx::fillRect(dl, *ctx.shapes, Rect{0, 0, ctx.width, ctx.height}, t.overlayDim);
    gfx::roundRect(dl, *ctx.shapes, l.card, 16.0f, t.bg);

    const gfx::FontFace& head = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    const gfx::FontFace& body = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    gfx::drawTextEllipsis(dl, head, l.textX, l.titleBaseline, l.textW, title_, t.text);
    const std::string_view all(text_);
    float y = l.textTop;
    for (int i = 0; i < l.count; ++i) {
        const std::string_view line = all.substr(l.offsets[i] + l.lines[i].start, l.lines[i].length);
        gfx::drawTextEllipsis(dl, body, l.textX, y + body.ascent, l.textW, line, t.text2);
        y += body.lineHeight;
    }

    const gfx::FontFace& label = ctx.fonts->face(FontSize::Body, FontWeight::Medium);
    for (int i = 0; i < count_; ++i) {
        const Rect& b = l.buttons[i];
        const bool focus = i == focus_;
        gfx::roundRect(dl, *ctx.shapes, b, kButtonH / 2, focus ? t.accent : t.accentSoft);
        const float inner = b.w - 16.0f;
        const float w = gfx::measureText(label, labels_[i]);
        const float x = w < inner ? b.x + (b.w - w) / 2 : b.x + 8.0f;
        gfx::drawTextEllipsis(dl, label, x, b.y + b.h / 2 + (label.ascent - label.descent) / 2, inner, labels_[i],
                              focus ? t.onAccent : t.accentSoftText);
    }
}

}  // namespace nl::ui
