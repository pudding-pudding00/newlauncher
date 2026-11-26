#include "ui/device_code_scene.hpp"

#include <cmath>
#include <cstdio>
#include <utility>

#include "gfx/text.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;

constexpr float kCardMaxW = 540.0f, kCardH = 280.0f, kPad = 24.0f;

void centered(gfx::DrawList& dl, const gfx::FontFace& f, const Rect& c, float baseline, std::string_view text,
              Color color) {
    const float tw = gfx::measureText(f, text);
    const float inner = c.w - 2 * kPad;
    gfx::drawTextEllipsis(dl, f, tw < inner ? c.x + (c.w - tw) / 2 : c.x + kPad, baseline, inner, text, color);
}
}  // namespace

void DeviceCodeScene::open() {
    url_.clear();
    userCode_.clear();
    status_.clear();
    remainingMs_ = 0.0f;
    closeRequested_ = false;
}

void DeviceCodeScene::setCode(std::string verifyUrl, std::string userCode, u32 expiresInS) {
    url_ = std::move(verifyUrl);
    userCode_ = std::move(userCode);
    remainingMs_ = static_cast<float>(expiresInS) * 1000.0f;
}

std::string DeviceCodeScene::remainingText() const {
    const unsigned s = remainingMs_ <= 0.0f ? 0u : static_cast<unsigned>(std::ceil(remainingMs_ / 1000.0f));
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02u:%02u", s / 60 > 99 ? 99u : s / 60, s % 60);
    return buf;
}

void DeviceCodeScene::update(const UiContext&, const InputFrame& in, float dtMs, SceneRequests& out) {
    if (!waiting() && remainingMs_ > 0.0f) remainingMs_ -= dtMs;
    if (closeRequested_) {  // bis open(): eine Szene unter einem Dialog schließt erst als oberste
        out.pop = true;
        return;
    }
    if (in.pressed(Action::Back) || in.pressed(Action::Home)) {
        out.pop = true;
        out.command = AppCommand::CloudCancelLink;
        out.sound = UiSound::Back;
    }
}

void DeviceCodeScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    gfx::fillRect(dl, s, Rect{0, 0, ctx.width, ctx.height}, t.overlayDim);
    const float w = ctx.width - 64.0f < kCardMaxW ? ctx.width - 64.0f : kCardMaxW;
    const Rect c{(ctx.width - w) / 2, (ctx.height - kCardH) / 2, w, kCardH};
    gfx::roundRect(dl, s, c, 16.0f, t.bg);
    const gfx::FontFace& head = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    const gfx::FontFace& body = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    const gfx::FontFace& strong = ctx.fonts->face(FontSize::Body, FontWeight::Medium);
    const gfx::FontFace& big = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    const gfx::FontFace& small = ctx.fonts->face(FontSize::Small, FontWeight::Regular);

    float y = c.y + kPad;
    centered(dl, head, c, y + head.ascent, tr(Str::CloudCodeTitle), t.text);
    y += head.lineHeight + 12.0f;
    if (waiting()) {
        centered(dl, body, c, y + body.ascent, status_.empty() ? tr(Str::CloudCodeWaiting) : status_, t.text2);
    } else {
        centered(dl, body, c, y + body.ascent, tr(Str::CloudCodeStep1), t.text2);
        y += body.lineHeight;
        centered(dl, strong, c, y + strong.ascent, url_, t.accent);
        y += strong.lineHeight + 6.0f;
        centered(dl, body, c, y + body.ascent, tr(Str::CloudCodeStep2), t.text2);
        y += body.lineHeight + 4.0f;
        centered(dl, big, c, y + big.ascent, userCode_, t.text);
        y += big.lineHeight + 6.0f;
        char line[64];
        std::snprintf(line, sizeof line, tr(Str::CloudCodeRemaining), remainingText().c_str());
        centered(dl, body, c, y + body.ascent, status_.empty() ? std::string_view(line) : status_, t.textMuted);
    }
    centered(dl, small, c, c.y + c.h - kPad / 2 - small.descent, tr(Str::CloudCodeHelp), t.textMuted);
}

}  // namespace nl::ui
