#include "gfx/draw_list.hpp"

namespace nl::gfx {

DrawList::DrawList(u32 maxQuads, u32 maxBatches)
    : maxQuads_(maxQuads),
      maxBatches_(maxBatches),
      vertices_(new Vertex[static_cast<std::size_t>(maxQuads) * 4]),
      batches_(new Batch[maxBatches]) {}

void DrawList::clear() {
    quadCount_ = batchCount_ = dropped_ = 0;
    alphaDepth_ = ignoredPushes_ = 0;
    alpha_ = 1.0f;
}

void DrawList::pushAlpha(float a) {
    if (alphaDepth_ >= kAlphaDepth) {
        ++ignoredPushes_;  // passendes popAlpha soll den Stapel nicht verschieben
        return;
    }
    alphaStack_[alphaDepth_++] = alpha_;
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    alpha_ *= a;
}

void DrawList::popAlpha() {
    if (ignoredPushes_ > 0) {
        --ignoredPushes_;
        return;
    }
    if (alphaDepth_ == 0) return;
    alpha_ = alphaStack_[--alphaDepth_];
}

void DrawList::quad(TextureId tex, Rect dst, UvRect uv, Color c) {
    if (quadCount_ >= maxQuads_) {
        ++dropped_;
        return;
    }
    const bool extend = batchCount_ > 0 && batches_[batchCount_ - 1].texture == tex;
    if (!extend) {
        if (batchCount_ >= maxBatches_) {
            ++dropped_;
            return;
        }
        batches_[batchCount_++] = Batch{tex, quadCount_ * 4, 0};
    }
    if (alpha_ < 1.0f) c.a = static_cast<u8>(static_cast<float>(c.a) * alpha_ + 0.5f);

    Vertex* v = &vertices_[static_cast<std::size_t>(quadCount_) * 4];
    const float x1 = dst.x + dst.w, y1 = dst.y + dst.h;
    v[0] = Vertex{dst.x, dst.y, uv.u0, uv.v0, c};
    v[1] = Vertex{x1, dst.y, uv.u1, uv.v0, c};
    v[2] = Vertex{x1, y1, uv.u1, uv.v1, c};
    v[3] = Vertex{dst.x, y1, uv.u0, uv.v1, c};
    ++quadCount_;
    batches_[batchCount_ - 1].vertexCount += 4;
}

}  // namespace nl::gfx
