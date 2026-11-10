#pragma once
// Video Interface: Videomodus aus den Systemeinstellungen, zwei Framebuffer, Präsentation.

#include <gccore.h>

namespace nl::platform {

class Video {
public:
    void init();
    GXRModeObj* mode() const { return mode_; }
    void* framebuffer(int index) const { return xfb_[index]; }
    bool widescreen() const { return widescreen_; }
    void present(int index);

private:
    GXRModeObj* mode_ = nullptr;
    void* xfb_[2] = {nullptr, nullptr};
    bool widescreen_ = false;
};

// Vor dem Chainload: Bild schwarz schalten und zwei Bilder abwarten, damit kein Restbild flackert.
void shutdownVideo();

}  // namespace nl::platform
