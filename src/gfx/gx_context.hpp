#pragma once
// GX-Grundgerüst: Initialisierung, Frame-Ablauf und ein Test-Quad.
// Logische Koordinaten 640x480, Ursprung oben links (16:9-Korrektur folgt in M1).

#include "core/color.hpp"
#include "platform/video.hpp"

namespace nl::gfx {

class GxContext {
public:
    void init(platform::Video& video);
    void beginFrame(Color clear);
    void fillRect(float x, float y, float w, float h, Color color);
    void endFrame();

private:
    platform::Video* video_ = nullptr;
    void* fifo_ = nullptr;
    int next_ = 1;
};

}  // namespace nl::gfx
