#include "ui/pointer_cursor.hpp"

namespace nl::ui {

void drawPointerCursor(const UiContext& ctx, gfx::DrawList& dl, const InputFrame& in) {
    if (!in.pointerValid || ctx.theme == nullptr || ctx.shapes == nullptr) return;
    // Heller Ring mit Akzentpunkt: auf hellem wie dunklem Hintergrund gut sichtbar.
    gfx::circle(dl, *ctx.shapes, in.pointerX, in.pointerY, 10.0f, Color{0xFF, 0xFF, 0xFF, 0xE6});
    gfx::circle(dl, *ctx.shapes, in.pointerX, in.pointerY, 7.0f, ctx.theme->accent);
}

}  // namespace nl::ui
