#pragma once
// Testschrift: jedes Glyph gleich breit, Leerzeichen ohne Bild. Erlaubt exakte Erwartungswerte.

#include "gfx/font.hpp"

namespace nl::test {

inline gfx::FontFace fakeFace(float advance = 10.0f, float lineHeight = 20.0f) {
    gfx::FontFace f{};
    f.ascent = lineHeight * 0.75f;
    f.descent = lineHeight * 0.25f;
    f.lineHeight = lineHeight;
    for (int i = 0; i < gfx::kGlyphCount; ++i) {
        gfx::Glyph& g = f.glyphs[i];
        g.uv = gfx::UvRect{0.0f, 0.0f, 0.01f, 0.01f};
        g.w = advance * 0.8f;
        g.h = lineHeight * 0.6f;
        g.bearingX = advance * 0.1f;
        g.bearingY = lineHeight * 0.6f;
        g.advance = advance;
    }
    f.glyphs[gfx::glyphIndex(' ')].w = 0.0f;
    f.glyphs[gfx::glyphIndex(' ')].h = 0.0f;
    return f;
}

inline gfx::FontSet fakeFontSet(float advance = 10.0f, float lineHeight = 20.0f) {
    gfx::FontSet s{};
    for (auto& size : s.faces)
        for (auto& face : size) face = fakeFace(advance, lineHeight);
    return s;
}

}  // namespace nl::test
