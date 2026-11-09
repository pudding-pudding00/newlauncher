// M1-Demo (Task 6): Atlas, Formen, Text und Eingabe-Aktionen.

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
#include "platform/wii_input.hpp"
#include "ui/input.hpp"

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

    nl::platform::WiiInput input;
    input.init(renderer.logicalWidth(), renderer.logicalHeight());
    nl::ui::InputMapper mapper;
    static const char* const kActionNames[] = {"Up", "Down", "Left", "Right", "Confirm", "Back",
        "PrevConsole", "NextConsole", "Favorite", "Search", "Home", "DebugToggle"};
    const char* lastAction = "-";

    using nl::gfx::FontSize;
    using nl::gfx::FontWeight;
    const nl::Theme& theme = nl::lightTheme();
    const nl::gfx::FontSet& fonts = atlas->fonts;
    nl::gfx::DrawList dl(4096, 16);

    bool readyReported = false;
    while (true) {
        const nl::ui::InputFrame in = mapper.update(input.poll(), 1000.0f / 60.0f);
        for (int a = 0; a < static_cast<int>(nl::ui::Action::Count); ++a)
            if (in.pressed(static_cast<nl::ui::Action>(a))) lastAction = kActionNames[a];
        if (in.pressed(nl::ui::Action::Home)) break;

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
        nl::gfx::drawText(dl, fonts.face(FontSize::Body, FontWeight::Medium), w / 2 - 130, 310, lastAction,
                          theme.accentSoftText);
        if (in.pointerValid) nl::gfx::circle(dl, atlas->shapes, in.pointerX, in.pointerY, 8, theme.accent);

        renderer.beginFrame(theme.bg);
        renderer.submit(dl);
        renderer.endFrame();

        if (!readyReported) {
            nl::log::line("NL_READY");
            readyReported = true;
        }
    }
    input.shutdown();
    return 0;
}
