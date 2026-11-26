#include "ui/cloud_sync_scene.hpp"

#include <cstdio>
#include <utility>

#include "gfx/text.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;

constexpr float kCardMaxW = 520.0f, kCardH = 170.0f, kPad = 24.0f;
}  // namespace

void CloudSyncScene::open(std::string title) {
    title_ = std::move(title);
    phase_ = CloudPhase::Checking;
    elapsedMs_ = 0.0f;
}

void CloudSyncScene::update(const UiContext&, const InputFrame& in, float dtMs, SceneRequests& out) {
    elapsedMs_ += dtMs;
    if (elapsedMs_ > 3600000.0f) elapsedMs_ = 0.0f;
    if (in.pressed(Action::Back) || in.pressed(Action::Home)) {
        out.pop = true;
        out.command = AppCommand::CloudSkip;
        out.sound = UiSound::Back;
    }
}

void CloudSyncScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    gfx::fillRect(dl, s, Rect{0, 0, ctx.width, ctx.height}, t.overlayDim);
    const float w = ctx.width - 64.0f < kCardMaxW ? ctx.width - 64.0f : kCardMaxW;
    const Rect c{(ctx.width - w) / 2, (ctx.height - kCardH) / 2, w, kCardH};
    gfx::roundRect(dl, s, c, 16.0f, t.bg);
    const gfx::FontFace& head = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    const gfx::FontFace& body = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    const gfx::FontFace& small = ctx.fonts->face(FontSize::Small, FontWeight::Regular);
    const float x = c.x + kPad, iw = c.w - 2 * kPad;
    float y = c.y + kPad;

    char title[160];
    std::snprintf(title, sizeof title, tr(Str::CloudSyncTitle), title_.c_str());
    gfx::drawTextEllipsis(dl, head, x, y + head.ascent, iw, title, t.text);
    y += head.lineHeight + 12.0f;
    gfx::drawTextEllipsis(dl, body, x, y + body.ascent, iw,
                          tr(phase_ == CloudPhase::Resolving ? Str::CloudSyncResolving : Str::CloudSyncChecking), t.text2);
    y += body.lineHeight + 10.0f;
    // drei Punkte, einer hervorgehoben (zeigt, dass der Launcher nicht hängt)
    const int lit = static_cast<int>(elapsedMs_ / 300.0f) % 3;
    for (int i = 0; i < 3; ++i) {
        const Rect dot{x + static_cast<float>(i) * 18.0f, y, 10.0f, 10.0f};
        gfx::roundRect(dl, s, dot, 5.0f, i == lit ? t.accent : t.accentSoft);
    }
    gfx::drawTextEllipsis(dl, small, x, c.y + c.h - kPad / 2 - small.descent, iw, tr(Str::CloudSyncSkip), t.textMuted);
}

}  // namespace nl::ui
