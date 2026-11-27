#pragma once
// Sprite-Batcher (Spec 4.1/4.4): die UI schreibt pro Frame Quads in eine Liste fester Kapazität,
// nur der Renderer kennt GX. Überzählige Quads werden verworfen und gezählt, nie überschrieben.

#include "core/color.hpp"
#include "core/types.hpp"

#include <memory>
#include <span>

namespace nl::gfx {

using TextureId = u16;  // 0 = UI-Atlas, ab kCoverTextureBase die Cover-Slots
inline constexpr TextureId kCoverTextureBase = 1;  // Cover-Slot s ist kCoverTextureBase + s
inline constexpr TextureId kBackgroundTexture = 0xFFFF;  // Hintergrundbild des Theme-Pakets (M17)
// Banner-Texturen (M18): kBannerTextureBase + Slot, Slots < kBannerTextureSlots; vor kBackgroundTexture.
inline constexpr TextureId kBannerTextureBase = 0xF000;
inline constexpr TextureId kBannerTextureSlots = 64;

struct Vertex {
    float x, y, u, v;
    Color color;
};
struct UvRect {
    float u0, v0, u1, v1;
};
struct Rect {
    float x, y, w, h;
};
struct Batch {
    TextureId texture;
    u32 firstVertex;
    u32 vertexCount;
    bool clipped = false;  // M18: nur innerhalb von clip zeichnen (logische Koordinaten)
    Rect clip{};
    bool silhouette = false;  // texture alpha only, colour from the vertices (focus ring around a cover)
};

class DrawList {
public:
    DrawList(u32 maxQuads, u32 maxBatches);

    void clear();
    // Ecken im Uhrzeigersinn ab links oben.
    void quad(TextureId tex, Rect dst, UvRect uv, Color c);
    // Freies Viereck (M18, Banner): Ecken im Uhrzeigersinn ab links oben, Alpha-Stapel wirkt wie bei quad.
    void quadVerts(TextureId tex, const Vertex (&v)[4]);
    // Like quad, but only the texture's alpha is used and the colour comes from c: a flat silhouette of the image.
    // Always its own batch, also for the same texture.
    void silhouette(TextureId tex, Rect dst, UvRect uv, Color c);

    // Clip-Rechteck für alle folgenden Quads bis clearClip/clear; ein Wechsel beginnt einen neuen Batch.
    void setClip(Rect r);
    void clearClip();

    // Multiplikativer Alpha-Stapel für Überblendungen (Tiefe 8).
    void pushAlpha(float a);
    void popAlpha();

    std::span<const Vertex> vertices() const { return {vertices_.get(), quadCount_ * 4}; }
    std::span<const Batch> batches() const { return {batches_.get(), batchCount_}; }
    u32 quadCount() const { return quadCount_; }
    u32 droppedQuads() const { return dropped_; }

private:
    static constexpr int kAlphaDepth = 8;

    Vertex* reserve(TextureId tex, bool silhouette = false);
    void put(Vertex* v, Rect dst, UvRect uv, Color c) const;  // v == nullptr: nothing  // nullptr: voll, gezählt in dropped_

    u32 maxQuads_, maxBatches_;
    std::unique_ptr<Vertex[]> vertices_;
    std::unique_ptr<Batch[]> batches_;
    u32 quadCount_ = 0, batchCount_ = 0, dropped_ = 0;
    float alphaStack_[kAlphaDepth] = {};
    int alphaDepth_ = 0;
    int ignoredPushes_ = 0;
    float alpha_ = 1.0f;
    bool clipped_ = false;
    Rect clip_{};
};

}  // namespace nl::gfx
