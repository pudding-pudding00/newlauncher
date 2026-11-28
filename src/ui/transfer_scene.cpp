#include "ui/transfer_scene.hpp"

#include <cstdio>

#include "gfx/text.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

void TransferScene::open(TransferView view) { view_ = std::move(view); }

void TransferScene::update(const UiContext&, const InputFrame& in, float, SceneRequests& out) {
    if (in.pressed(Action::Back)) {
        out.pop = true;
        out.command = AppCommand::CloseTransfer;
        out.sound = UiSound::Back;
    }
}

void TransferScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
    if (ctx.theme == nullptr || ctx.fonts == nullptr || ctx.shapes == nullptr) return;
    const Theme& t = *ctx.theme;
    const float w = 500.0f;
    const float h = 250.0f;
    const float x = (ctx.width - w) * 0.5f;
    const float y = (ctx.height - h) * 0.5f;
    const float textW = w - 40.0f;

    gfx::fillRect(dl, *ctx.shapes, gfx::Rect{0, 0, ctx.width, ctx.height}, t.overlayDim);
    gfx::roundRect(dl, *ctx.shapes, gfx::Rect{x, y, w, h}, 16.0f, t.bg);

    const gfx::FontFace& titleFont = ctx.fonts->face(gfx::FontSize::Title, gfx::FontWeight::Medium);
    const gfx::FontFace& bodyFont = ctx.fonts->face(gfx::FontSize::Body, gfx::FontWeight::Regular);
    const gfx::FontFace& smallFont = ctx.fonts->face(gfx::FontSize::Small, gfx::FontWeight::Regular);

    float cy = y + 22.0f + titleFont.ascent;
    gfx::drawTextEllipsis(dl, titleFont, x + 20.0f, cy, textW, tr(Str::TransferTitle), t.text);
    cy += 32.0f;

    char buf[192];
    if (!view_.listening) {
        gfx::drawTextEllipsis(dl, bodyFont, x + 20.0f, cy, textW, tr(Str::TransferNoNet), t.textMuted);
        cy += 24.0f;
    } else {
        std::snprintf(buf, sizeof buf, tr(Str::TransferAddress), view_.uri.c_str());
        gfx::drawTextEllipsis(dl, bodyFont, x + 20.0f, cy, textW, buf, t.text);
        cy += 24.0f;

        if (view_.customPassword) {
            std::snprintf(buf, sizeof buf, tr(Str::TransferUserCustom), view_.user.c_str());
        } else {
            std::snprintf(buf, sizeof buf, tr(Str::TransferUserPin), view_.user.c_str(), view_.pin.c_str());
        }
        gfx::drawTextEllipsis(dl, bodyFont, x + 20.0f, cy, textW, buf, t.text);
        cy += 24.0f;
    }

    std::string mountLine;
    for (std::size_t i = 0; i < view_.mounts.size(); ++i) {
        if (i > 0) mountLine += ", ";
        mountLine += "/" + view_.mounts[i].name + (view_.mounts[i].writable ? " (RW)" : " (RO)");
    }
    if (!mountLine.empty()) {
        std::snprintf(buf, sizeof buf, tr(Str::TransferMounts), mountLine.c_str());
        gfx::drawTextEllipsis(dl, smallFont, x + 20.0f, cy, textW, buf, t.textMuted);
        cy += 24.0f;
    }

    if (view_.transferring) {
        const unsigned kb = static_cast<unsigned>((view_.activeBytes + 1023u) / 1024u);
        std::snprintf(buf, sizeof buf, tr(Str::TransferActive),
                      view_.activeOp.c_str(), view_.activePath.c_str(), kb);
        gfx::drawTextEllipsis(dl, bodyFont, x + 20.0f, cy, textW, buf, t.accent);
    } else if (view_.clientConnected) {
        std::snprintf(buf, sizeof buf, tr(Str::TransferConnected), view_.peerIp.c_str());
        gfx::drawTextEllipsis(dl, bodyFont, x + 20.0f, cy, textW, buf, t.accent);
    } else if (view_.listening) {
        gfx::drawTextEllipsis(dl, bodyFont, x + 20.0f, cy, textW, tr(Str::TransferIdle), t.textMuted);
    }

    gfx::drawTextEllipsis(dl, smallFont, x + 20.0f, y + h - 16.0f, textW, tr(Str::TransferHelp), t.textMuted);
}

}  // namespace nl::ui
