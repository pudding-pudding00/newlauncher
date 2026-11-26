#include "ui/pairing_scene.hpp"

#include <cmath>
#include <cstdio>
#include <utility>

#include "gfx/text.hpp"
#include "ui/qr_view.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;

constexpr float kCardMaxW = 600.0f, kPad = 20.0f, kQrSide = 210.0f;

void centered(gfx::DrawList& dl, const gfx::FontFace& f, const Rect& c, float baseline, std::string_view text,
              Color color) {
    const float tw = gfx::measureText(f, text);
    const float inner = c.w - 2 * kPad;
    gfx::drawTextEllipsis(dl, f, tw < inner ? c.x + (c.w - tw) / 2 : c.x + kPad, baseline, inner, text, color);
}
}  // namespace

void PairingScene::open() {
    pairUrl_.clear();
    authorizeUrl_.clear();
    status_.clear();
    pairQr_.reset();
    directQr_.reset();
    remainingMs_ = 0.0f;
    direct_ = false;
    closeRequested_ = false;
}

void PairingScene::setLink(std::string pairUrl, std::string authorizeUrl, u32 expiresInS) {
    pairUrl_ = std::move(pairUrl);
    authorizeUrl_ = std::move(authorizeUrl);
    pairQr_ = pairUrl_.empty() ? std::nullopt : encodeQr(pairUrl_);
    directQr_ = encodeQr(authorizeUrl_);  // ~150 Zeichen: passt in Version 10
    remainingMs_ = static_cast<float>(expiresInS) * 1000.0f;
    direct_ = false;
}

std::string PairingScene::remainingText() const {
    const unsigned s = remainingMs_ <= 0.0f ? 0u : static_cast<unsigned>(std::ceil(remainingMs_ / 1000.0f));
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02u:%02u", s / 60 > 99 ? 99u : s / 60, s % 60);
    return buf;
}

void PairingScene::update(const UiContext&, const InputFrame& in, float dtMs, SceneRequests& out) {
    if (!waiting() && remainingMs_ > 0.0f) remainingMs_ -= dtMs;
    if (closeRequested_) {
        out.pop = true;
        return;
    }
    if (in.pressed(Action::Back) || in.pressed(Action::Home)) {
        out.pop = true;
        out.command = AppCommand::CloudCancelLink;
        out.sound = UiSound::Back;
        return;
    }
    if (waiting()) return;
    if (in.pressed(Action::Favorite) && !pairUrl_.empty()) {  // Y bzw. 1
        direct_ = !direct_;
        out.sound = UiSound::Move;
    }
    if (in.pressed(Action::Confirm)) {
        out.command = AppCommand::CloudEnterCode;
        out.sound = UiSound::Confirm;
    }
}

void PairingScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    gfx::fillRect(dl, s, Rect{0, 0, ctx.width, ctx.height}, t.overlayDim);
    const gfx::FontFace& head = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    const gfx::FontFace& body = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    const gfx::FontFace& strong = ctx.fonts->face(FontSize::Body, FontWeight::Medium);
    const gfx::FontFace& small = ctx.fonts->face(FontSize::Small, FontWeight::Regular);

    const float w = ctx.width - 32.0f < kCardMaxW ? ctx.width - 32.0f : kCardMaxW;
    const float h = kPad + head.lineHeight + 8 + kQrSide + 8 + body.lineHeight + strong.lineHeight + body.lineHeight +
                    small.lineHeight + kPad;
    const Rect c{(ctx.width - w) / 2, (ctx.height - h) / 2, w, h};
    gfx::roundRect(dl, s, c, 16.0f, t.bg);

    float y = c.y + kPad;
    centered(dl, head, c, y + head.ascent, tr(Str::PairTitle), t.text);
    y += head.lineHeight + 8;
    const Rect qrArea{c.x + (c.w - kQrSide) / 2, y, kQrSide, kQrSide};
    const std::optional<QrCode>& qr = directQr() ? directQr_ : pairQr_;
    if (waiting() || !qr) {
        centered(dl, body, c, y + kQrSide / 2, status_.empty() ? tr(Str::PairWaiting) : status_, t.text2);
    } else {
        drawQr(dl, s, *qr, qrArea);
    }
    y += kQrSide + 8;
    if (!waiting()) {
        if (directQr()) {
            centered(dl, body, c, y + body.ascent, tr(Str::PairScanDirect), t.text2);
            y += body.lineHeight + strong.lineHeight;  // die lange Dropbox-URL wäre nicht abzutippen
        } else {
            centered(dl, body, c, y + body.ascent, tr(Str::PairScanPhone), t.text2);
            y += body.lineHeight;
            centered(dl, strong, c, y + strong.ascent, pairUrl_, t.accent);
            y += strong.lineHeight;
        }
        char line[64];
        std::snprintf(line, sizeof line, tr(Str::CloudCodeRemaining), remainingText().c_str());
        centered(dl, body, c, y + body.ascent, status_.empty() ? std::string_view(line) : status_,
                 status_.empty() ? t.textMuted : t.accent);
    }
    centered(dl, small, c, c.y + c.h - kPad / 2 - small.descent, tr(Str::PairHelp), t.textMuted);
}

}  // namespace nl::ui
