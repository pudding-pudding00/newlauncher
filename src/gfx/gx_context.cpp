#include "gfx/gx_context.hpp"

#include <cstdlib>
#include <cstring>

#include <gccore.h>

namespace nl::gfx {
namespace {

constexpr u32 kFifoSize = 256 * 1024;

GXColor toGx(Color c) { return GXColor{c.r, c.g, c.b, c.a}; }

}  // namespace

void GxContext::init(platform::Video& video) {
    video_ = &video;
    GXRModeObj* mode = video.mode();

    fifo_ = std::aligned_alloc(32, kFifoSize);
    std::memset(fifo_, 0, kFifoSize);
    GX_Init(fifo_, kFifoSize);

    GX_SetViewport(0, 0, mode->fbWidth, mode->efbHeight, 0, 1);
    const f32 yscale = GX_GetYScaleFactor(mode->efbHeight, mode->xfbHeight);
    const u32 xfbHeight = GX_SetDispCopyYScale(yscale);
    GX_SetScissor(0, 0, mode->fbWidth, mode->efbHeight);
    GX_SetDispCopySrc(0, 0, mode->fbWidth, mode->efbHeight);
    GX_SetDispCopyDst(mode->fbWidth, xfbHeight);
    GX_SetCopyFilter(mode->aa, mode->sample_pattern, GX_TRUE, mode->vfilter);
    GX_SetFieldMode(mode->field_rendering,
                    (mode->viHeight == 2 * mode->xfbHeight) ? GX_ENABLE : GX_DISABLE);
    GX_SetPixelFmt(mode->aa ? GX_PF_RGB565_Z16 : GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetDispCopyGamma(GX_GM_1_0);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);

    // Vertexformat: Position (XY, f32) + Farbe (RGBA8).
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);

    // Nur Vertexfarbe, keine Textur.
    GX_SetNumChans(1);
    GX_SetNumTexGens(0);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE,
                   GX_AF_NONE);

    Mtx44 proj;
    guOrtho(proj, 0, 479, 0, 639, 0, 300);
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);

    Mtx view;
    guMtxIdentity(view);
    GX_LoadPosMtxImm(view, GX_PNMTX0);
}

void GxContext::beginFrame(Color clear) {
    GX_SetCopyClear(toGx(clear), GX_MAX_Z24);
}

void GxContext::fillRect(float x, float y, float w, float h, Color c) {
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2f32(x, y);
    GX_Color4u8(c.r, c.g, c.b, c.a);
    GX_Position2f32(x + w, y);
    GX_Color4u8(c.r, c.g, c.b, c.a);
    GX_Position2f32(x + w, y + h);
    GX_Color4u8(c.r, c.g, c.b, c.a);
    GX_Position2f32(x, y + h);
    GX_Color4u8(c.r, c.g, c.b, c.a);
    GX_End();
}

void GxContext::endFrame() {
    GX_DrawDone();
    GX_SetColorUpdate(GX_TRUE);
    GX_CopyDisp(video_->framebuffer(next_), GX_TRUE);
    GX_Flush();
    video_->present(next_);
    next_ ^= 1;
}

}  // namespace nl::gfx
