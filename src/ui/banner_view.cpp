#include "ui/banner_view.hpp"

namespace nl::ui {

void drawBanner(gfx::DrawList& dl, const gfx::AtlasShapes& shapes, gfx::Rect dst, const BannerFrame& frame) {
    if (frame.width <= 0.0f || frame.height <= 0.0f) return;
    const float scale = dst.w / frame.width;
    const float cx = dst.x + dst.w / 2.0f, cy = dst.y + dst.h / 2.0f;
    // Weißes Atlas-Feld: Mitte nehmen, damit lineare Filterung nicht in den Rand greift.
    const float wu = (shapes.white.u0 + shapes.white.u1) / 2.0f, wv = (shapes.white.v0 + shapes.white.v1) / 2.0f;
    static constexpr int kOrder[4] = {0, 1, 3, 2};  // Quad: TL, TR, BL, BR → DrawList: im Uhrzeigersinn
    dl.setClip(dst);
    for (const Quad& q : frame.quads) {
        gfx::TextureId tex = 0;
        if (q.texIndex >= 0) {
            if (q.texIndex >= gfx::kBannerTextureSlots) continue;
            tex = static_cast<gfx::TextureId>(gfx::kBannerTextureBase + q.texIndex);
        }
        gfx::Vertex v[4];
        for (int i = 0; i < 4; ++i) {
            const int k = kOrder[i];
            const float u = tex == 0 ? wu : q.uv[k].x, vv = tex == 0 ? wv : q.uv[k].y;
            v[i] = gfx::Vertex{cx + q.pos[k].x * scale, cy - q.pos[k].y * scale, u, vv, q.color[k]};
        }
        dl.quadVerts(tex, v);
    }
    dl.clearClip();
}

}  // namespace nl::ui
