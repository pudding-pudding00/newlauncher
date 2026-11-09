#include "gfx/font_loader.hpp"

#include <cstdlib>
#include <cstring>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <gccore.h>

#include "MPLUSRounded1c-Medium-latin1_ttf.h"
#include "MPLUSRounded1c-Regular-latin1_ttf.h"
#include "gfx/atlas.hpp"

namespace nl::gfx {

namespace {

constexpr u16 kCircleDiameter = 64;
constexpr u16 kWhiteSize = 8;

struct EmbeddedFont {
    const u8* data;
    std::size_t size;
};

UvRect uvOf(u16 x, u16 y, u16 w, u16 h) {
    return UvRect{static_cast<float>(x) / kAtlasWidth, static_cast<float>(y) / kAtlasHeight,
                  static_cast<float>(x + w) / kAtlasWidth, static_cast<float>(y + h) / kAtlasHeight};
}

u32 codepointOf(int index) {
    if (index < 95) return 0x20 + static_cast<u32>(index);
    if (index < 191) return 0xA0 + static_cast<u32>(index - 95);
    return 0x2026;
}

Error ftError(const char* what, FT_Error e) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s (FreeType %d)", what, static_cast<int>(e));
    return Error{ErrorCode::InvalidData, buf};
}

// Rendert alle Glyphen einer Größe; liefert false, wenn der Atlas voll ist.
Result<void> renderFace(FT_Face face, int px, ShelfPacker& packer, u8* linear, FontFace& out) {
    if (FT_Error e = FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(px))) {
        return Result<void>::err(ftError("Schriftgröße", e));
    }
    out.ascent = static_cast<float>(face->size->metrics.ascender >> 6);
    out.descent = static_cast<float>(-(face->size->metrics.descender >> 6));
    out.lineHeight = static_cast<float>(face->size->metrics.height >> 6);

    for (int i = 0; i < kGlyphCount; ++i) {
        Glyph& g = out.glyphs[i];
        g = Glyph{};
        if (FT_Error e = FT_Load_Char(face, codepointOf(i), FT_LOAD_RENDER)) {
            return Result<void>::err(ftError("Glyph", e));
        }
        const FT_GlyphSlot slot = face->glyph;
        const FT_Bitmap& bm = slot->bitmap;
        g.advance = static_cast<float>(slot->advance.x >> 6);
        g.bearingX = static_cast<float>(slot->bitmap_left);
        g.bearingY = static_cast<float>(slot->bitmap_top);
        if (bm.width == 0 || bm.rows == 0) continue;

        u16 x = 0, y = 0;
        if (!packer.place(static_cast<u16>(bm.width), static_cast<u16>(bm.rows), x, y)) {
            return Result<void>::err(Error{ErrorCode::InvalidData, "UI-Atlas voll"});
        }
        for (unsigned row = 0; row < bm.rows; ++row) {
            std::memcpy(linear + (y + row) * kAtlasWidth + x, bm.buffer + row * bm.pitch, bm.width);
        }
        g.w = static_cast<float>(bm.width);
        g.h = static_cast<float>(bm.rows);
        g.uv = uvOf(x, y, static_cast<u16>(bm.width), static_cast<u16>(bm.rows));
    }
    return Result<void>::ok();
}

Result<void> fillAtlas(UiAtlas& atlas, u8* linear) {
    ShelfPacker packer(kAtlasWidth, kAtlasHeight);
    u16 x = 0, y = 0;

    // Weißer Block: UVs nur auf den inneren Bereich, damit die Filterung keinen Rand mischt.
    if (!packer.place(kWhiteSize, kWhiteSize, x, y)) return Result<void>::err(Error{ErrorCode::InvalidData, "UI-Atlas voll"});
    for (u16 r = 0; r < kWhiteSize; ++r) std::memset(linear + (y + r) * kAtlasWidth + x, 0xFF, kWhiteSize);
    atlas.shapes.white = uvOf(x + 2, y + 2, kWhiteSize - 4, kWhiteSize - 4);

    if (!packer.place(kCircleDiameter, kCircleDiameter, x, y)) return Result<void>::err(Error{ErrorCode::InvalidData, "UI-Atlas voll"});
    rasterCircle(linear, kAtlasWidth, x, y, kCircleDiameter);
    atlas.shapes.circle = uvOf(x, y, kCircleDiameter, kCircleDiameter);

    const EmbeddedFont fonts[2] = {
        {MPLUSRounded1c_Regular_latin1_ttf, MPLUSRounded1c_Regular_latin1_ttf_size},
        {MPLUSRounded1c_Medium_latin1_ttf, MPLUSRounded1c_Medium_latin1_ttf_size},
    };

    FT_Library lib = nullptr;
    if (FT_Error e = FT_Init_FreeType(&lib)) return Result<void>::err(ftError("FreeType-Init", e));
    Result<void> result = Result<void>::ok();
    for (int w = 0; w < 2 && result.isOk(); ++w) {
        FT_Face face = nullptr;
        if (FT_Error e = FT_New_Memory_Face(lib, fonts[w].data, static_cast<FT_Long>(fonts[w].size), 0, &face)) {
            result = Result<void>::err(ftError("Schrift laden", e));
            break;
        }
        for (int s = 0; s < 3 && result.isOk(); ++s) {
            result = renderFace(face, kFontSizePx[s], packer, linear, atlas.fonts.faces[s][w]);
        }
        FT_Done_Face(face);
    }
    FT_Done_FreeType(lib);
    return result;
}

}  // namespace

UiAtlas::~UiAtlas() { std::free(pixelsI8); }

Result<std::unique_ptr<UiAtlas>> buildUiAtlas() {
    using R = Result<std::unique_ptr<UiAtlas>>;
    constexpr std::size_t kBytes = static_cast<std::size_t>(kAtlasWidth) * kAtlasHeight;

    auto atlas = std::unique_ptr<UiAtlas>(new UiAtlas());
    u8* linear = static_cast<u8*>(std::calloc(kBytes, 1));
    atlas->pixelsI8 = static_cast<u8*>(std::aligned_alloc(32, kBytes));
    if (linear == nullptr || atlas->pixelsI8 == nullptr) {
        std::free(linear);
        return R::err(Error{ErrorCode::InvalidData, "kein Speicher für den UI-Atlas"});
    }

    Result<void> filled = fillAtlas(*atlas, linear);
    if (filled.isOk()) {
        linearToI8(linear, kAtlasWidth, kAtlasHeight, atlas->pixelsI8);
        DCFlushRange(atlas->pixelsI8, kBytes);
    }
    std::free(linear);
    if (!filled.isOk()) return R::err(filled.error());
    return R::ok(std::move(atlas));
}

}  // namespace nl::gfx
