#include "ui/startup_scene.hpp"

#include <cstdio>

#include "gfx/text.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;

constexpr float kButtonW = 200.0f, kButtonH = 48.0f, kButtonGap = 24.0f;

bool contains(Rect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

void centered(gfx::DrawList& dl, const gfx::FontFace& f, float width, float baseline, const char* text, Color c) {
    const float w = gfx::measureText(f, text);
    gfx::drawTextEllipsis(dl, f, w < width - 32 ? (width - w) / 2 : 16, baseline, width - 32, text, c);
}
}  // namespace

void StartupScene::showWaitingUsb(int secondsLeft) {
    phase_ = Phase::WaitingUsb;
    std::snprintf(status_, sizeof status_, "%s (%d s)", kStrWaitUsb, secondsLeft < 0 ? 0 : secondsLeft);
}

void StartupScene::showUsbFailed() {
    // main ruft das im Zustand „Zeitüberschreitung" jeden Frame auf: nur beim Wechsel zurücksetzen,
    // sonst wäre „SD verwenden" per Steuerkreuz nie erreichbar.
    if (phase_ == Phase::UsbFailed) return;
    phase_ = Phase::UsbFailed;
    focus_ = 0;
    keysSincePointerMove_ = false;
    std::snprintf(status_, sizeof status_, "%s", kStrNoUsb);
}

void StartupScene::showScanning(u32 filesSeen) {
    phase_ = Phase::Scanning;
    std::snprintf(status_, sizeof status_, "%s %u %s", kStrScanning, static_cast<unsigned>(filesSeen), kStrFiles);
}

StartupScene::Choice StartupScene::takeChoice() {
    const Choice c = choice_;
    choice_ = Choice::None;
    return c;
}

Rect StartupScene::buttonRect(const UiContext& ctx, int index) const {
    const float x0 = (ctx.width - (2 * kButtonW + kButtonGap)) / 2;
    return Rect{x0 + static_cast<float>(index) * (kButtonW + kButtonGap), ctx.height / 2 + 24, kButtonW, kButtonH};
}

void StartupScene::update(const UiContext& ctx, const InputFrame& in, float, SceneRequests&) {
    if (phase_ != Phase::UsbFailed) return;
    int hit = -1;
    if (in.pointerValid) {
        for (int i = 0; i < 2; ++i)
            if (contains(buttonRect(ctx, i), in.pointerX, in.pointerY)) hit = i;
        if (in.pointerMoved) {
            if (hit >= 0) focus_ = hit;
            keysSincePointerMove_ = false;
        }
    }
    if (in.pressed(Action::Left) && focus_ > 0) --focus_;
    if (in.pressed(Action::Right) && focus_ < 1) ++focus_;
    if (in.pressed(Action::Left) || in.pressed(Action::Right)) keysSincePointerMove_ = true;
    const bool pointerOff = in.pointerValid && hit < 0 && !keysSincePointerMove_;
    if (in.pressed(Action::Confirm) && !pointerOff) choice_ = focus_ == 0 ? Choice::Retry : Choice::UseSd;
}

void StartupScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    gfx::fillRect(dl, *ctx.shapes, Rect{0, 0, ctx.width, ctx.height}, t.bg);
    const gfx::FontFace& title = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    const gfx::FontFace& body = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    centered(dl, title, ctx.width, ctx.height / 2 - 40, "newlauncher", t.text);
    if (status_[0] != '\0') centered(dl, body, ctx.width, ctx.height / 2, status_, t.text2);
    if (phase_ != Phase::UsbFailed) return;

    const char* labels[2] = {kStrRetry, kStrUseSd};
    const gfx::FontFace& label = ctx.fonts->face(FontSize::Body, FontWeight::Medium);
    for (int i = 0; i < 2; ++i) {
        const Rect r = buttonRect(ctx, i);
        const bool focused = i == focus_;
        gfx::roundRect(dl, *ctx.shapes, r, kButtonH / 2, focused ? t.accent : t.tile);
        const float w = gfx::measureText(label, labels[i]);
        gfx::drawTextEllipsis(dl, label, w < r.w - 16 ? r.x + (r.w - w) / 2 : r.x + 8,
                              r.y + r.h / 2 + (label.ascent - label.descent) / 2, r.w - 16, labels[i],
                              focused ? t.onAccent : t.text);
    }
}

}  // namespace nl::ui
