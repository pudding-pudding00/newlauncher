// „Hallo GX“ (M0): Hintergrund in Theme-Farbe, Akzent-Rechteck, NL_READY nach dem ersten Frame.

#include <gccore.h>
#include <wiiuse/wpad.h>

#include "core/color.hpp"
#include "gfx/gx_context.hpp"
#include "platform/log.hpp"
#include "platform/video.hpp"

namespace {

constexpr nl::Color kBackground = nl::Color::fromHex(0xEEF1F4);
constexpr nl::Color kAccent = nl::Color::fromHex(0x34BEED);

}  // namespace

int main(int, char**) {
    nl::log::init();
    nl::log::line("newlauncher %s", NL_VERSION);

    nl::platform::Video video;
    video.init();
    nl::gfx::GxContext gx;
    gx.init(video);

    WPAD_Init();
    PAD_Init();

    bool readyReported = false;
    while (true) {
        WPAD_ScanPads();
        PAD_ScanPads();
        if ((WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME) || (PAD_ButtonsDown(0) & PAD_BUTTON_START)) {
            return 0;
        }

        gx.beginFrame(kBackground);
        gx.fillRect(220, 180, 200, 120, kAccent);
        gx.endFrame();

        if (!readyReported) {
            nl::log::line("NL_READY");
            readyReported = true;
        }
    }
}
