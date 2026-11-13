#pragma once
// BRLYT-Layout aus Wii-Kanalbannern (M18). Formatquelle: mkwiiki „BRLYT (File Format)“ und WiiBrew
// „Wii Animations“, nur als Beschreibung gelesen. Gelesen werden lyt1, txl1, mat1 (erste Textur-Map,
// erste Textur-SRT, Materialfarbe), pan1, pic1 und der Baum aus pas1/pae1. txt1, wnd1 und bnd1 werden
// zu Gruppen-Panes ohne eigenes Bild (ihre Kinder bleiben im Baum); alle anderen Abschnitte werden übersprungen.
// Jeder übersprungene oder nur als Gruppe gelesene Abschnitt erhöht Layout::skipped.

#include <array>
#include <span>
#include <string>
#include <vector>

#include "core/color.hpp"
#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

struct Vec2 {
    float x = 0, y = 0;
    friend constexpr bool operator==(const Vec2&, const Vec2&) = default;
};
struct Vec3 {
    float x = 0, y = 0, z = 0;
};
using Rgba = Color;

inline constexpr u32 kLytMaxPanes = 1024;
inline constexpr u32 kLytMaxMaterials = 1024;
inline constexpr u32 kLytMaxTextures = 256;
inline constexpr u32 kLytNameMax = 255;  // Texturnamen in txl1

struct LytMaterial {
    std::string name;
    s16 texIndex = -1;                                  // erste Textur-Map, -1 ohne Textur
    std::array<float, 5> texSrt{0, 0, 0, 1, 1};         // tx, ty, rot (Grad), sx, sy
    Rgba color{255, 255, 255, 255};                     // Materialfarbe; ohne Block weiß
};

// Ursprung: horizontal origin % 3 (0 links, 1 Mitte, 2 rechts), vertikal origin / 3 (0 oben, 1 Mitte, 2 unten).
struct LytPane {
    std::string name;
    s32 parent = -1;                                    // Index in Layout::panes, Eltern stehen immer davor
    bool visible = true;
    bool influencedAlpha = false;                       // Alpha wirkt auf die Kinder
    u8 alpha = 255;
    Vec3 trans, rot;                                    // rot in Grad, gezeichnet wird nur rot.z
    Vec2 scale{1, 1}, size;
    u8 origin = 4;
    bool isPicture = false;
    s32 material = -1;
    std::array<Rgba, 4> vtxColor{Rgba{255, 255, 255, 255}, Rgba{255, 255, 255, 255}, Rgba{255, 255, 255, 255},
                                 Rgba{255, 255, 255, 255}};  // oben links, oben rechts, unten links, unten rechts
    std::array<Vec2, 4> uv{Vec2{0, 0}, Vec2{1, 0}, Vec2{0, 1}, Vec2{1, 1}};
};

struct Layout {
    float width = 0, height = 0;
    std::vector<std::string> textures;
    std::vector<LytMaterial> materials;
    std::vector<LytPane> panes;
    u32 skipped = 0;
};

Result<Layout> parseBrlyt(std::span<const u8> file);

}  // namespace nl
