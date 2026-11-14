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
    clipped_ = false;
}

void DrawList::setClip(Rect r) {
    clipped_ = true;
    clip_ = r;
}

void DrawList::clearClip() { clipped_ = false; }

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

Vertex* DrawList::reserve(TextureId tex) {
    if (quadCount_ >= maxQuads_) {
        ++dropped_;
        return nullptr;
    }
    const Batch* last = batchCount_ > 0 ? &batches_[batchCount_ - 1] : nullptr;
    const bool sameClip = last != nullptr && last->clipped == clipped_ &&
                          (!clipped_ || (last->clip.x == clip_.x && last->clip.y == clip_.y && last->clip.w == clip_.w &&
                                         last->clip.h == clip_.h));
    if (last == nullptr || last->texture != tex || !sameClip) {
        if (batchCount_ >= maxBatches_) {
            ++dropped_;
            return nullptr;
        }
        batches_[batchCount_++] = Batch{tex, quadCount_ * 4, 0, clipped_, clip_};
    }
    Vertex* v = &vertices_[static_cast<std::size_t>(quadCount_) * 4];
    ++quadCount_;
    batches_[batchCount_ - 1].vertexCount += 4;
    return v;
}

void DrawList::quad(TextureId tex, Rect dst, UvRect uv, Color c) {
    Vertex* v = reserve(tex);
    if (v == nullptr) return;
    if (alpha_ < 1.0f) c.a = static_cast<u8>(static_cast<float>(c.a) * alpha_ + 0.5f);
    const float x1 = dst.x + dst.w, y1 = dst.y + dst.h;
    v[0] = Vertex{dst.x, dst.y, uv.u0, uv.v0, c};
    v[1] = Vertex{x1, dst.y, uv.u1, uv.v0, c};
    v[2] = Vertex{x1, y1, uv.u1, uv.v1, c};
    v[3] = Vertex{dst.x, y1, uv.u0, uv.v1, c};
}

void DrawList::quadVerts(TextureId tex, const Vertex (&src)[4]) {
    Vertex* v = reserve(tex);
    if (v == nullptr) return;
    for (int i = 0; i < 4; ++i) {
        v[i] = src[i];
        if (alpha_ < 1.0f) v[i].color.a = static_cast<u8>(static_cast<float>(v[i].color.a) * alpha_ + 0.5f);
    }
}

}  // namespace nl::gfx
