#include "gfx/renderer.hpp"

#include <cstdlib>
#include <cstring>

#include <gccore.h>

namespace nl::gfx {

namespace {
constexpr u32 kFifoSize = 256 * 1024;
GXTexObj g_atlasTex;

GXTexObj g_coverTex;
GXTexObj g_backgroundTex;

GXColor toGx(Color c) { return GXColor{c.r, c.g, c.b, c.a}; }

// UI-Atlas (I8): Farbe = Rasterfarbe, Alpha = Rasteralpha × Texturintensität.
void useAtlasTev() {
    GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
    GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_RASA, GX_CA_TEXA, GX_CA_ZERO);
    GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
}
}  // namespace

Result<void> Renderer::init(platform::Video& video) {
    video_ = &video;
    GXRModeObj* mode = video.mode();
    logicalWidth_ = video.widescreen() ? 854.0f : 640.0f;
    pixelScaleX_ = static_cast<float>(mode->fbWidth) / logicalWidth_;

    fifo_ = std::aligned_alloc(32, kFifoSize);
    if (fifo_ == nullptr) return Result<void>::err(Error{ErrorCode::InvalidData, "kein Speicher für die GX-FIFO"});
    std::memset(fifo_, 0, kFifoSize);
    GX_Init(fifo_, kFifoSize);

    GX_SetViewport(0, 0, mode->fbWidth, mode->efbHeight, 0, 1);
    const f32 yscale = GX_GetYScaleFactor(mode->efbHeight, mode->xfbHeight);
    const u32 xfbHeight = GX_SetDispCopyYScale(yscale);
    GX_SetScissor(0, 0, mode->fbWidth, mode->efbHeight);
    GX_SetDispCopySrc(0, 0, mode->fbWidth, mode->efbHeight);
    GX_SetDispCopyDst(mode->fbWidth, xfbHeight);
    GX_SetCopyFilter(mode->aa, mode->sample_pattern, GX_TRUE, mode->vfilter);
    GX_SetFieldMode(mode->field_rendering, (mode->viHeight == 2 * mode->xfbHeight) ? GX_ENABLE : GX_DISABLE);
    GX_SetPixelFmt(mode->aa ? GX_PF_RGB565_Z16 : GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetDispCopyGamma(GX_GM_1_0);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetAlphaUpdate(GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);

    // Vertexformat: Position XY f32, Farbe RGBA8, Texturkoordinate ST f32.
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

    // Eine TEV-Stufe, je nach Textur Atlas- oder Cover-Modus (submit).
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    useAtlasTev();

    // Logisches Koordinatensystem: 640 bzw. 854 × 480, auf die EFB-Größe skaliert.
    Mtx44 proj;
    guOrtho(proj, 0, logicalHeight(), 0, logicalWidth_, 0, 300);
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
    Mtx view;
    guMtxIdentity(view);
    GX_LoadPosMtxImm(view, GX_PNMTX0);
    return Result<void>::ok();
}

void Renderer::setAtlas(const UiAtlas& atlas) {
    GX_InitTexObj(&g_atlasTex, atlas.pixelsI8, kAtlasWidth, kAtlasHeight, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&g_atlasTex, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GX_InvalidateTexAll();
    GX_LoadTexObj(&g_atlasTex, GX_TEXMAP0);
    textureBytes_ = static_cast<u32>(kAtlasWidth) * kAtlasHeight;
}

void Renderer::beginFrame(Color clear) {
    GX_SetCopyClear(toGx(clear), GX_MAX_Z24);
    if (!efbCleared_) {
        // Der EFB wird erst beim Kopieren gelöscht; vor dem ersten Frame einmal leer kopieren,
        // damit der erste sichtbare Frame nicht auf Speicherresten gezeichnet wird.
        GX_CopyDisp(video_->framebuffer(next_), GX_TRUE);
        GX_Flush();
        efbCleared_ = true;
    }
    drawCalls_ = 0;
}

void Renderer::setBackground(const void* texels, u16 width, u16 height) {
    background_ = texels;
    if (texels != nullptr) {
        GX_InitTexObj(&g_backgroundTex, const_cast<void*>(texels), width, height, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP,
                      GX_FALSE);
        GX_InitTexObjLOD(&g_backgroundTex, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    }
    GX_InvalidateTexAll();  // gleiche Adresse kann jetzt ein anderes Bild sein
}

void Renderer::submit(const DrawList& list, const CoverTextures* covers) {
    // Cover-Slots wurden neu belegt oder freigegeben: gleiche Adresse kann jetzt anderes Bild sein.
    if (covers != nullptr && covers->generation() != coverGeneration_) {
        coverGeneration_ = covers->generation();
        GX_InvalidateTexAll();
    }
    const auto vertices = list.vertices();
    TextureId bound = 0;
    for (const Batch& b : list.batches()) {
        if (b.vertexCount == 0) continue;
        if (b.texture != bound) {
            if (b.texture == 0) {
                GX_LoadTexObj(&g_atlasTex, GX_TEXMAP0);
                useAtlasTev();
            } else if (b.texture == kBackgroundTexture) {
                if (background_ == nullptr) continue;
                GX_LoadTexObj(&g_backgroundTex, GX_TEXMAP0);
                GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
            } else {
                const void* texels = covers != nullptr ? covers->texels(b.texture) : nullptr;
                if (texels == nullptr) continue;
                GX_InitTexObj(&g_coverTex, const_cast<void*>(texels), 200, 280, GX_TF_CMPR, GX_CLAMP, GX_CLAMP,
                              GX_FALSE);
                GX_InitTexObjLOD(&g_coverTex, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
                GX_LoadTexObj(&g_coverTex, GX_TEXMAP0);
                GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);  // Texel × Vertexfarbe, Alpha für die Einblendung
            }
            bound = b.texture;
        }
        GX_Begin(GX_QUADS, GX_VTXFMT0, static_cast<u16>(b.vertexCount));
        for (u32 i = 0; i < b.vertexCount; ++i) {
            const Vertex& v = vertices[b.firstVertex + i];
            GX_Position2f32(v.x, v.y);
            GX_Color4u8(v.color.r, v.color.g, v.color.b, v.color.a);
            GX_TexCoord2f32(v.u, v.v);
        }
        GX_End();
        ++drawCalls_;
    }
    if (bound != 0) {  // nächster Frame beginnt wieder mit dem Atlas
        GX_LoadTexObj(&g_atlasTex, GX_TEXMAP0);
        useAtlasTev();
    }
}

void Renderer::endFrame() {
    GX_DrawDone();
    GX_CopyDisp(video_->framebuffer(next_), GX_TRUE);
    GX_Flush();
    video_->present(next_);
    next_ ^= 1;
    drawCallsLast_ = drawCalls_;
}

}  // namespace nl::gfx
