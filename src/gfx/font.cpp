#include "gfx/font.hpp"

namespace nl::gfx {

int glyphIndex(u32 cp) {
    if (cp >= 0x20 && cp <= 0x7E) return static_cast<int>(cp - 0x20);
    if (cp >= 0xA0 && cp <= 0xFF) return 95 + static_cast<int>(cp - 0xA0);
    if (cp == 0x2026) return 191;
    return '?' - 0x20;
}

const Glyph& FontFace::glyph(u32 codepoint) const { return glyphs[glyphIndex(codepoint)]; }

}  // namespace nl::gfx
