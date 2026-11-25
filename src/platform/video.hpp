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
    // Bildwiederholrate des gewählten Modus: 50 Hz nur bei PAL 576i/288p, EURGB60 und NTSC haben 60 Hz.
    int refreshHz() const {
        const u32 m = mode_ != nullptr ? mode_->viTVMode : 0;
        return (m == VI_TVMODE_PAL_INT || m == VI_TVMODE_PAL_DS) ? 50 : 60;
    }
    void present(int index);
    // Zuletzt angezeigter Puffer (ungecacht), z. B. für den Screenshot der Fernsteuerung (M20).
    const void* shownFramebuffer() const { return xfb_[shown_]; }

private:
    GXRModeObj* mode_ = nullptr;
    void* xfb_[2] = {nullptr, nullptr};
    bool widescreen_ = false;
    int shown_ = 0;
};

// Vor dem Chainload: Bild schwarz schalten und zwei Bilder abwarten, damit kein Restbild flackert.
void shutdownVideo();

}  // namespace nl::platform
