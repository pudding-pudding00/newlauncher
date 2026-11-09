#pragma once
// GX-Backend (Spec 3.10/4.1): übersetzt eine DrawList in GX-Aufrufe, ein GX_Begin pro Batch.

#include "core/result.hpp"
#include "gfx/draw_list.hpp"
#include "gfx/font_loader.hpp"
#include "platform/video.hpp"

namespace nl::gfx {

class Renderer {
public:
    Result<void> init(platform::Video& video);
    void setAtlas(const UiAtlas& atlas);

    float logicalWidth() const { return logicalWidth_; }
    float logicalHeight() const { return 480.0f; }

    void beginFrame(Color clear);
    void submit(const DrawList& list);
    void endFrame();  // VSync + Pufferwechsel

    u32 drawCallsLastFrame() const { return drawCallsLast_; }
    u32 textureBytes() const { return textureBytes_; }

private:
    platform::Video* video_ = nullptr;
    void* fifo_ = nullptr;
    float logicalWidth_ = 640.0f;
    int next_ = 1;
    bool efbCleared_ = false;
    u32 drawCalls_ = 0, drawCallsLast_ = 0, textureBytes_ = 0;
};

}  // namespace nl::gfx
