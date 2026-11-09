#pragma once
// Schriftmodell (Spec 3.7): drei Größen, zwei Schnitte, Glyphen für Latin-1 plus „…".

#include "core/types.hpp"
#include "gfx/draw_list.hpp"

#include <cstddef>
#include <string_view>

namespace nl::gfx {

enum class FontSize : u8 { Small, Body, Title };  // 12, 16, 22 logische Pixel
enum class FontWeight : u8 { Regular, Medium };
inline constexpr int kFontSizePx[3] = {12, 16, 22};

// 0x20–0x7E (95) + 0xA0–0xFF (96) + U+2026 (1)
inline constexpr int kGlyphCount = 192;

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

// Liest einen Codepoint ab `pos` und rückt `pos` um mindestens 1 vor.
// Ungültige, überlange, abgeschnittene Folgen und Surrogate ergeben '?'.
u32 decodeUtf8(std::string_view s, std::size_t& pos);

}  // namespace nl::gfx
