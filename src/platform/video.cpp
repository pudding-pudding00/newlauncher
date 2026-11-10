#include "platform/video.hpp"

namespace nl::platform {

void Video::init() {
    VIDEO_Init();
    mode_ = VIDEO_GetPreferredMode(nullptr);
    widescreen_ = CONF_GetAspectRatio() == CONF_ASPECT_16_9;

    for (auto& fb : xfb_) {
        fb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode_));
        VIDEO_ClearFrameBuffer(mode_, fb, COLOR_BLACK);  // kein Speichermüll im ersten Bild
    }

    VIDEO_Configure(mode_);
    VIDEO_SetNextFramebuffer(xfb_[0]);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (mode_->viTVMode & VI_NON_INTERLACE) {
        VIDEO_WaitVSync();
    }
}

void Video::present(int index) {
    VIDEO_SetNextFramebuffer(xfb_[index]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
}

void shutdownVideo() {
    VIDEO_SetBlack(true);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    VIDEO_WaitVSync();
}

}  // namespace nl::platform
