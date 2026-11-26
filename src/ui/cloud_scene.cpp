#include "ui/cloud_scene.hpp"

#include <cstdio>
#include <utility>

#include "core/civil_time.hpp"
#include "gfx/text.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;

constexpr float kCardMaxW = 520.0f, kCardH = 250.0f, kPad = 24.0f, kRowH = 34.0f;

bool contains(Rect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

std::string stamp(u64 unix) {
    if (unix == 0) return tr(Str::CloudUnknownTime);
    const CivilTime t = civilFromUnix(unix);
    char buf[32];
    if (language() == Lang::De) {
        std::snprintf(buf, sizeof buf, "%02u.%02u.%04d %02u:%02u", t.day, t.month, t.year, t.hour, t.minute);
    } else {
        std::snprintf(buf, sizeof buf, "%04d-%02u-%02u %02u:%02u", t.year, t.month, t.day, t.hour, t.minute);
    }
    return buf;
}
}  // namespace

std::string conflictText(u64 wiiPlayed, u64 cloudSaved, std::string_view device) {
    std::string s = "Wii: " + stamp(wiiPlayed) + " · Cloud: " + stamp(cloudSaved);
    if (!device.empty()) s += ", " + std::string(device);
    return s;
}

void CloudScene::open(CloudView v) {
    view_ = v;
    message_.clear();
    focus_ = kRowProvider;
    fixFocus();
}

void CloudScene::setView(CloudView v) {
    view_ = v;
    fixFocus();
}

bool CloudScene::rowEnabled(int row) const {
    const bool usable = view_.status != CloudView::Status::NoTls && !view_.busy;
    switch (row) {
        case kRowProvider: return usable;
        case kRowLink:
            return usable && view_.provider == CloudProviderId::Drive && view_.status != CloudView::Status::NoClientId;
        case kRowUnlink:
            return usable && (view_.status == CloudView::Status::Linked || view_.status == CloudView::Status::Relink);
        default: return false;
    }
}

void CloudScene::fixFocus() {
    if (rowEnabled(focus_)) return;
    for (int r = 0; r < kRowCount; ++r) {
        if (rowEnabled(r)) {
            focus_ = r;
            return;
        }
    }
}

const char* CloudScene::statusText() const {
    if (view_.status == CloudView::Status::NoTls) return tr(Str::CloudNoTls);
    if (view_.busy) return tr(Str::CloudBusy);
    if (view_.provider == CloudProviderId::None) return tr(Str::CloudStatusOff);
    switch (view_.status) {
        case CloudView::Status::Linked: return tr(Str::CloudLinked);
        case CloudView::Status::Relink: return tr(Str::CloudRelink);
        case CloudView::Status::NoClientId: return tr(Str::CloudNoClientId);
        default: return tr(Str::CloudNotLinked);
    }
}

Rect CloudScene::cardRect(const UiContext& ctx) const {
    const float w = ctx.width - 64.0f < kCardMaxW ? ctx.width - 64.0f : kCardMaxW;
    return Rect{(ctx.width - w) / 2, (ctx.height - kCardH) / 2, w, kCardH};
}

Rect CloudScene::rowRect(const UiContext& ctx, int row) const {
    const Rect c = cardRect(ctx);
    return Rect{c.x + kPad, c.y + kPad + 36.0f + static_cast<float>(row) * kRowH, c.w - 2 * kPad, kRowH};
}

void CloudScene::update(const UiContext& ctx, const InputFrame& in, float, SceneRequests& out) {
    if (in.pressed(Action::Back) || in.pressed(Action::Home)) {
        out.pop = true;
        out.sound = UiSound::Back;
        return;
    }
    const int dirV = in.pressed(Action::Down) ? 1 : in.pressed(Action::Up) ? -1 : 0;
    if (dirV != 0) {
        for (int r = focus_ + dirV; r >= 0 && r < kRowCount; r += dirV) {
            if (!rowEnabled(r)) continue;
            focus_ = r;
            out.sound = UiSound::Move;
            break;
        }
    }
    int hit = -1;
    if (in.pointerValid) {
        for (int r = 0; r < kRowCount; ++r)
            if (rowEnabled(r) && contains(rowRect(ctx, r), in.pointerX, in.pointerY)) hit = r;
        if (in.pointerMoved && hit >= 0) focus_ = hit;
    }
    fixFocus();
    if (!rowEnabled(focus_)) return;
    const bool confirm = in.pressed(Action::Confirm) && (!in.pointerValid || hit >= 0);
    if (focus_ == kRowProvider) {
        if (confirm || in.pressed(Action::Right) || in.pressed(Action::Left)) {
            view_.provider = view_.provider == CloudProviderId::None ? CloudProviderId::Drive : CloudProviderId::None;
            out.command = AppCommand::CloudSetProvider;
            out.sound = UiSound::Move;
        }
        return;
    }
    if (!confirm) return;
    out.command = focus_ == kRowLink ? AppCommand::CloudLink : AppCommand::CloudUnlink;
    out.sound = UiSound::Confirm;
}

void CloudScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    gfx::fillRect(dl, s, Rect{0, 0, ctx.width, ctx.height}, t.overlayDim);
    const Rect c = cardRect(ctx);
    gfx::roundRect(dl, s, c, 16.0f, t.bg);
    const gfx::FontFace& head = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    const gfx::FontFace& body = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    const gfx::FontFace& value = ctx.fonts->face(FontSize::Body, FontWeight::Medium);
    const gfx::FontFace& small = ctx.fonts->face(FontSize::Small, FontWeight::Regular);
    const float x = c.x + kPad, w = c.w - 2 * kPad;
    gfx::drawTextEllipsis(dl, head, x, c.y + kPad + head.ascent, w, tr(Str::Cloud), t.text);

    static constexpr Str kLabels[kRowCount] = {Str::CloudProvider, Str::CloudLinkRow, Str::CloudUnlinkRow};
    for (int r = 0; r < kRowCount; ++r) {
        const Rect row = rowRect(ctx, r);
        const bool enabled = rowEnabled(r);
        if (r == focus_ && enabled) gfx::roundRect(dl, s, Rect{row.x - 4, row.y + 2, row.w + 8, row.h - 4}, 10, t.tileFocus);
        const float base = row.y + row.h / 2 + (body.ascent - body.descent) / 2;
        const Color fg = enabled ? (r == kRowProvider ? t.text : t.accent) : t.textMuted;
        gfx::drawTextEllipsis(dl, body, row.x + 8, base, row.w / 2, tr(kLabels[r]), fg);
        if (r == kRowProvider) {
            const char* v = tr(view_.provider == CloudProviderId::Drive ? Str::CloudDrive : Str::CloudOff);
            const float vw = gfx::measureText(value, v);
            gfx::drawText(dl, value, row.x + row.w - 8 - vw, base, v, enabled ? t.text : t.textMuted);
        }
    }
    float y = rowRect(ctx, kRowCount - 1).y + kRowH + 8.0f;
    gfx::drawTextEllipsis(dl, body, x, y + body.ascent, w, statusText(), t.text2);
    y += body.lineHeight;
    if (!message_.empty()) gfx::drawTextEllipsis(dl, body, x, y + body.ascent, w, message_, t.text2);
    gfx::drawTextEllipsis(dl, small, x, c.y + c.h - kPad / 2 - small.descent, w, tr(Str::CloudHelp), t.textMuted);
}

}  // namespace nl::ui
