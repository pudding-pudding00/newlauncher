#include "ui/dialog_scene.hpp"

#include <span>
#include <string_view>

#include "gfx/text.hpp"
#include "ui/launch_text.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;

constexpr float kCardMaxW = 440.0f, kPad = 24.0f, kButtonW = 160.0f, kButtonH = 44.0f, kGap = 20.0f;

bool contains(Rect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }
}  // namespace

struct DialogScene::Layout {
    Rect card, button;
    float textX, textW, titleBaseline, textTop;
    gfx::Line lines[kMaxLines];
    u16 offsets[kMaxLines];  // Anfang des Absatzes, auf den sich lines[i] bezieht
    int count;
};

void DialogScene::open(const char* title, const char* text) {
    copyEllipsized(title_, sizeof title_, title != nullptr ? title : "");
    copyEllipsized(text_, sizeof text_, text != nullptr ? text : "");
}

DialogScene::Layout DialogScene::layout(const UiContext& ctx) const {
    Layout l{};
    const gfx::FontFace& head = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    const gfx::FontFace& body = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    const float cardW = ctx.width - 64.0f < kCardMaxW ? ctx.width - 64.0f : kCardMaxW;
    l.textW = cardW - 2 * kPad;

    // Absätze an '\n' trennen, jeden für sich umbrechen.
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
    l.button = Rect{l.card.x + (cardW - kButtonW) / 2, l.card.y + cardH - kPad - kButtonH, kButtonW, kButtonH};
    return l;
}

Rect DialogScene::buttonRect(const UiContext& ctx) const { return layout(ctx).button; }

void DialogScene::update(const UiContext& ctx, const InputFrame& in, float, SceneRequests& out) {
    if (in.pressed(Action::Back)) out.pop = true;
    if (in.pressed(Action::Confirm)) {
        // Mit sichtbarem Zeiger zählt A nur auf dem Knopf, ohne Zeiger überall.
        if (!in.pointerValid || contains(buttonRect(ctx), in.pointerX, in.pointerY)) out.pop = true;
    }
}

void DialogScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
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
    gfx::roundRect(dl, *ctx.shapes, l.button, kButtonH / 2, t.accent);
    const float w = gfx::measureText(label, tr(Str::Ok));
    gfx::drawText(dl, label, l.button.x + (l.button.w - w) / 2,
                  l.button.y + l.button.h / 2 + (label.ascent - label.descent) / 2, tr(Str::Ok), t.onAccent);
}

}  // namespace nl::ui
