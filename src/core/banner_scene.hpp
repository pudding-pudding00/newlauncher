#pragma once
// Szene eines Kanal-Banners (M18): Pane-Zustand je Bild → Quads in Layout-Koordinaten (Ursprung Mitte, +Y oben).
// Eltern-Transformationen werden vererbt, Alpha nur bei influencedAlpha. Unsichtbare Panes und ihre Kinder
// erzeugen keine Quads; ebenso Bilder mit Textur-Index außerhalb der txl1-Liste.

#include <array>
#include <span>
#include <vector>

#include "core/brlyt.hpp"

namespace nl {

struct Quad {
    std::array<Vec2, 4> pos;  // oben links, oben rechts, unten links, unten rechts
    std::array<Vec2, 4> uv;
    std::array<Rgba, 4> color;
    s16 texIndex = -1;        // -1: ohne Textur, nur Farbe
};

struct PaneState {
    Vec3 trans, rot;
    Vec2 scale, size;
    u8 alpha = 255;
    bool visible = true;
    std::array<Rgba, 4> vtxColor;
    std::array<float, 5> texSrt{0, 0, 0, 1, 1};
    s16 texIndex = -1;
    Rgba matColor{255, 255, 255, 255};
};

std::vector<PaneState> initialState(const Layout& l);
// state hat so viele Einträge wie l.panes; sonst leeres Ergebnis.
std::vector<Quad> buildQuads(const Layout& l, std::span<const PaneState> state);

}  // namespace nl
