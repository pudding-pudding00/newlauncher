#pragma once
// Zeichnet ein Bild eines Kanal-Banners (M18) in ein Rechteck der Detailansicht: Layout-Breite → Rechteckbreite,
// gleichmäßig skaliert und zentriert, +Y des Layouts zeigt nach oben. Alles außerhalb des Rechtecks schneidet
// der Clip ab. Quads ohne Textur werden mit dem weißen Atlas-Feld gefärbt.

#include <span>

#include "core/banner_scene.hpp"
#include "gfx/draw_list.hpp"
#include "gfx/shapes.hpp"

namespace nl::ui {

struct BannerFrame {
    float width = 0.0f, height = 0.0f;  // Layout-Größe (lyt1)
    std::span<const Quad> quads;
};

void drawBanner(gfx::DrawList& dl, const gfx::AtlasShapes& shapes, gfx::Rect dst, const BannerFrame& frame);

}  // namespace nl::ui
