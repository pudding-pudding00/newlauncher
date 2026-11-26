#pragma once
// Zeichnet einen QR-Code (M29) als weiße Fläche mit 4 Modulen Ruhezone, ganzzahlig skaliert und zentriert in area.
// Dunkle Module einer Zeile werden zu einem Rechteck zusammengefasst (Version 10: wenige hundert statt ~1600 Quads).

#include "core/qr.hpp"
#include "gfx/draw_list.hpp"
#include "gfx/shapes.hpp"

namespace nl::ui {

void drawQr(gfx::DrawList& dl, const gfx::AtlasShapes& shapes, const QrCode& qr, gfx::Rect area);

}  // namespace nl::ui
