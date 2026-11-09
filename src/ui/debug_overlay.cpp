#include "ui/debug_overlay.hpp"

#include "gfx/text.hpp"

namespace nl::ui {

void DebugOverlay::setStats(const FrameStats& s) {
    formatStats(s, text_, sizeof text_);  // auch unsichtbar: beim Einschalten sofort aktuelle Werte
}

void DebugOverlay::draw(const UiContext& ctx, gfx::DrawList& dl) const {
    if (!visible_) return;
    const gfx::FontFace& f = ctx.fonts->face(gfx::FontSize::Small, gfx::FontWeight::Regular);
    const float w = gfx::measureText(f, text_) + 16.0f;
    gfx::roundRect(dl, *ctx.shapes, gfx::Rect{8, 8, w, 24}, 6, Color{0, 0, 0, 0xB0});
    gfx::drawText(dl, f, 16, 20 + (f.ascent - f.descent) / 2, text_, Color{0xFF, 0xFF, 0xFF, 0xFF});
}

}  // namespace nl::ui
