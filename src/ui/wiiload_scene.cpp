#include "ui/wiiload_scene.hpp"

#include <cstdio>

#include "gfx/text.hpp"
#include "ui/strings.hpp"

namespace nl::ui {
namespace {

const char* kindLabel(WiiloadKind k) {
    switch (k) {
        case WiiloadKind::Elf: return "ELF";
        case WiiloadKind::Zip: return "ZIP";
        case WiiloadKind::Dol: break;
    }
    return "DOL";
}

}  // namespace

void WiiloadScene::open(WiiloadSummary summary) {
    summary_ = std::move(summary);
    focus_ = 0;
}

void WiiloadScene::update(const UiContext&, const InputFrame& in, float, SceneRequests& out) {
    if (in.pressed(Action::Left)) {
        focus_ = (focus_ + kButtonCount - 1) % kButtonCount;
        out.sound = UiSound::Move;
    } else if (in.pressed(Action::Right)) {
        focus_ = (focus_ + 1) % kButtonCount;
        out.sound = UiSound::Move;
    } else if (in.pressed(Action::Back)) {
        out.pop = true;
        out.command = AppCommand::WiiloadDecline;
        out.sound = UiSound::Back;
    } else if (in.pressed(Action::Confirm)) {
        out.pop = true;
        if (focus_ == 0) {
            out.command = AppCommand::WiiloadAccept;
            out.sound = UiSound::Confirm;
        } else if (focus_ == 1) {
            out.command = AppCommand::WiiloadTrustSender;
            out.sound = UiSound::Confirm;
        } else {
            out.command = AppCommand::WiiloadDecline;
            out.sound = UiSound::Back;
        }
    }
}

void WiiloadScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
    if (ctx.theme == nullptr || ctx.fonts == nullptr || ctx.shapes == nullptr) return;
    const Theme& t = *ctx.theme;
    const float w = 480.0f;
    const float h = 236.0f;
    const float x = (ctx.width - w) * 0.5f;
    const float y = (ctx.height - h) * 0.5f;
    const float textW = w - 40.0f;

    gfx::fillRect(dl, *ctx.shapes, gfx::Rect{0, 0, ctx.width, ctx.height}, t.overlayDim);
    gfx::roundRect(dl, *ctx.shapes, gfx::Rect{x, y, w, h}, 16.0f, t.bg);

    const gfx::FontFace& titleFont = ctx.fonts->face(gfx::FontSize::Title, gfx::FontWeight::Medium);
    const gfx::FontFace& bodyFont = ctx.fonts->face(gfx::FontSize::Body, gfx::FontWeight::Regular);
    const gfx::FontFace& smallFont = ctx.fonts->face(gfx::FontSize::Small, gfx::FontWeight::Regular);

    float cy = y + 22.0f + titleFont.ascent;
    gfx::drawTextEllipsis(dl, titleFont, x + 20.0f, cy, textW, tr(Str::WiiloadTitle), t.text);
    cy += 30.0f;

    char buf[160];
    std::snprintf(buf, sizeof buf, tr(Str::WiiloadFrom),
                  summary_.senderIp.empty() ? "?" : summary_.senderIp.c_str());
    gfx::drawTextEllipsis(dl, bodyFont, x + 20.0f, cy, textW, buf, t.text);
    cy += 22.0f;

    const unsigned kb = (summary_.sizeBytes + 1023u) / 1024u;
    std::snprintf(buf, sizeof buf, tr(Str::WiiloadFile),
                  summary_.fileName.empty() ? "boot.dol" : summary_.fileName.c_str(),
                  kindLabel(summary_.kind), kb);
    gfx::drawTextEllipsis(dl, bodyFont, x + 20.0f, cy, textW, buf, t.text);
    cy += 22.0f;

    if (!summary_.targetApp.empty()) {
        std::snprintf(buf, sizeof buf, tr(Str::WiiloadTarget), summary_.targetApp.c_str());
        gfx::drawTextEllipsis(dl, bodyFont, x + 20.0f, cy, textW, buf, t.text);
        cy += 22.0f;
    }

    if (summary_.args.size() > 1) {
        std::string argLine;
        for (std::size_t i = 1; i < summary_.args.size(); ++i) {
            if (i > 1) argLine += ' ';
            argLine += summary_.args[i];
            if (argLine.size() > 48) {
                argLine.resize(45);
                argLine += "...";
                break;
            }
        }
        std::snprintf(buf, sizeof buf, tr(Str::WiiloadArgs), argLine.c_str());
        gfx::drawTextEllipsis(dl, smallFont, x + 20.0f, cy, textW, buf, t.textMuted);
    }

    const Str btnLabels[kButtonCount] = {Str::WiiloadBtnRun, Str::WiiloadBtnTrust, Str::WiiloadBtnDecline};
    const float btnW = 136.0f;
    const float btnH = 38.0f;
    const float gap = 12.0f;
    const float bx0 = x + (w - (btnW * kButtonCount + gap * (kButtonCount - 1))) * 0.5f;
    const float by = y + h - 72.0f;
    for (int i = 0; i < kButtonCount; ++i) {
        const gfx::Rect r{bx0 + i * (btnW + gap), by, btnW, btnH};
        const bool sel = (i == focus_);
        gfx::roundRect(dl, *ctx.shapes, r, 10.0f, sel ? t.accent : t.tile);
        const char* lbl = tr(btnLabels[i]);
        const float tw = gfx::measureText(bodyFont, lbl);
        const float tx = r.x + (r.w - tw) * 0.5f;
        const float ty = r.y + (r.h - bodyFont.lineHeight) * 0.5f + bodyFont.ascent;
        gfx::drawTextEllipsis(dl, bodyFont, tx > r.x + 6.0f ? tx : r.x + 6.0f, ty, r.w - 12.0f, lbl,
                              sel ? t.onAccent : t.text);
    }

    gfx::drawTextEllipsis(dl, smallFont, x + 20.0f, y + h - 14.0f, textW, tr(Str::WiiloadHelp), t.textMuted);
}

}  // namespace nl::ui
