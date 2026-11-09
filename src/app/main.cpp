// M1-Demo (Task 5): Atlas, Formen und Text über Renderer und DrawList.

#include <gccore.h>
#include <wiiuse/wpad.h>

#include <cstdlib>

#include "core/theme.hpp"
#include "gfx/draw_list.hpp"
#include "gfx/font_loader.hpp"
#include "gfx/renderer.hpp"
#include "gfx/shapes.hpp"
#include "gfx/text.hpp"
#include "platform/log.hpp"
#include "platform/video.hpp"

int main(int, char**) {
    nl::log::init();
    nl::log::line("newlauncher %s", NL_VERSION);

    nl::platform::Video video;
    video.init();
    nl::gfx::Renderer renderer;
    if (auto r = renderer.init(video); !r.isOk()) {
        nl::log::line("NL_ERROR %s", r.error().context.c_str());
        return EXIT_FAILURE;
    }
    auto atlasResult = nl::gfx::buildUiAtlas();
    if (!atlasResult.isOk()) {
        nl::log::line("NL_ERROR %s", atlasResult.error().context.c_str());
        return EXIT_FAILURE;
    }
    const auto atlas = std::move(atlasResult).value();
    renderer.setAtlas(*atlas);
    nl::log::line("NL_ATLAS ok");

    WPAD_Init();
    PAD_Init();

    using nl::gfx::FontSize;
    using nl::gfx::FontWeight;
    const nl::Theme& theme = nl::lightTheme();
    const nl::gfx::FontSet& fonts = atlas->fonts;
    nl::gfx::DrawList dl(4096, 16);

    bool readyReported = false;
    while (true) {
        WPAD_ScanPads();
        PAD_ScanPads();
        if ((WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME) || (PAD_ButtonsDown(0) & PAD_BUTTON_START)) break;

        dl.clear();
        const float w = renderer.logicalWidth();
        nl::gfx::roundRect(dl, atlas->shapes, nl::gfx::Rect{w / 2 - 150, 140, 300, 200}, 14, theme.tile);
        nl::gfx::circle(dl, atlas->shapes, w / 2 + 110, 180, 20, theme.accent);
        nl::gfx::drawText(dl, fonts.face(FontSize::Title, FontWeight::Medium), w / 2 - 130, 200,
                          "Zäh & flüssig - ÄÖÜß …", theme.text);
        nl::gfx::drawText(dl, fonts.face(FontSize::Body, FontWeight::Regular), w / 2 - 130, 240,
                          "The Legend of Zelda: Ocarina of Time", theme.text);
        nl::gfx::drawText(dl, fonts.face(FontSize::Small, FontWeight::Regular), w / 2 - 130, 270,
                          "N64 · 1998 · 1 Spieler", theme.text2);

        renderer.beginFrame(theme.bg);
        renderer.submit(dl);
        renderer.endFrame();

        if (!readyReported) {
            nl::log::line("NL_READY");
            readyReported = true;
        }
    }
    WPAD_Shutdown();
    return 0;
}
