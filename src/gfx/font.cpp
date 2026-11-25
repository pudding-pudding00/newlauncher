#include "gfx/font.hpp"

namespace nl::gfx {

int glyphIndex(u32 cp) {
    if (cp >= 0x20 && cp <= 0x7E) return static_cast<int>(cp - 0x20);
    if (cp >= 0xA0 && cp <= 0xFF) return 95 + static_cast<int>(cp - 0xA0);
    for (int i = 0; i < kGlyphCount - 191; ++i) {
        if (kExtraGlyphs[i] == cp) return 191 + i;
    }
    return '?' - 0x20;
}

u32 glyphCodepoint(int index) {
    if (index < 95) return 0x20 + static_cast<u32>(index);
    if (index < 191) return 0xA0 + static_cast<u32>(index - 95);
    return kExtraGlyphs[index - 191];
}

const Glyph& FontFace::glyph(u32 codepoint) const { return glyphs[glyphIndex(codepoint)]; }

}  // namespace nl::gfx
