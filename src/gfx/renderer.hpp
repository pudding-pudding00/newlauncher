#pragma once
// GX-Backend (Spec 3.10/4.1): übersetzt eine DrawList in GX-Aufrufe, ein GX_Begin pro Batch.

#include "core/result.hpp"
#include "gfx/draw_list.hpp"
#include "gfx/font_loader.hpp"
#include "gfx/texture_table.hpp"
#include "platform/video.hpp"

namespace nl::gfx {

class Renderer {
public:
    Result<void> init(platform::Video& video);
    void setAtlas(const UiAtlas& atlas);
    // Hintergrund (RGB5A3, 32-Byte-ausgerichtet, vom Aufrufer gehalten); nullptr: Batches mit kBackgroundTexture entfallen.
    void setBackground(const void* texels, u16 width, u16 height);

    float logicalWidth() const { return logicalWidth_; }
    float logicalHeight() const { return 480.0f; }
    float pixelScaleX() const { return pixelScaleX_; }

    void beginFrame(Color clear);
    // Batches mit Textur ≥ kCoverTextureBase holen ihre CMPR-Texel aus covers (nullptr: auslassen).
    void submit(const DrawList& list, const CoverTextures* covers = nullptr);
    void endFrame();  // VSync + Pufferwechsel

    u32 drawCallsLastFrame() const { return drawCallsLast_; }
    u32 textureBytes() const { return textureBytes_; }

private:
    platform::Video* video_ = nullptr;
    void* fifo_ = nullptr;
    float logicalWidth_ = 640.0f;
    float pixelScaleX_ = 1.0f;
    int next_ = 1;
    bool efbCleared_ = false;
    u32 coverGeneration_ = 0;
    const void* background_ = nullptr;
    u32 drawCalls_ = 0, drawCallsLast_ = 0, textureBytes_ = 0;
};

}  // namespace nl::gfx
