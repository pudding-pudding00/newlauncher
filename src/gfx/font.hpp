#pragma once
// Schriftmodell (Spec 3.7): drei Größen, zwei Schnitte, Glyphen für Latin-1 plus Zusatzzeichen.

#include "core/types.hpp"
#include "core/utf8.hpp"
#include "gfx/draw_list.hpp"

#include <cstddef>
#include <string_view>

namespace nl::gfx {

enum class FontSize : u8 { Small, Body, Title };  // 12, 16, 22 logische Pixel
enum class FontWeight : u8 { Regular, Medium };
inline constexpr int kFontSizePx[3] = {12, 16, 22};

// Zeichen außerhalb von Latin-1, in Atlas-Reihenfolge ab Index 191 (tools/subset_fonts.py nimmt dieselben).
inline constexpr u32 kExtraGlyphs[] = {0x2026, 0x2013, 0x2014, 0x2192, 0x25B2, 0x25B6, 0x25BC, 0x25C0};
// 0x20–0x7E (95) + 0xA0–0xFF (96) + Zusatzzeichen („…", –, —, →, ▲, ▶, ▼, ◀)
inline constexpr int kGlyphCount = 191 + static_cast<int>(sizeof(kExtraGlyphs) / sizeof(kExtraGlyphs[0]));

struct Glyph {
    UvRect uv;
    float w, h;      // Bildgröße in logischen Pixeln (0 = nichts zu zeichnen)
    float bearingX;  // Versatz vom Stift nach rechts
    float bearingY;  // Abstand Grundlinie → Oberkante
    float advance;
};

struct FontFace {
    float ascent, descent, lineHeight;
    // Echte Bildpunkte je logischem Pixel in x (16:9: 640/854). 0 = Glyphen nicht einrasten.
    float pixelScaleX = 0.0f;
    Glyph glyphs[kGlyphCount];

    const Glyph& glyph(u32 codepoint) const;
};

struct FontSet {
    FontFace faces[3][2];
    const FontFace& face(FontSize s, FontWeight w) const {
        return faces[static_cast<int>(s)][static_cast<int>(w)];
    }
};

int glyphIndex(u32 codepoint);  // unbekannt → Index von '?'
u32 glyphCodepoint(int index);  // Umkehrung von glyphIndex für 0 ≤ index < kGlyphCount

using nl::decodeUtf8;  // liegt in core/utf8.hpp

}  // namespace nl::gfx
