// Hauptschleife (M1): Eingabe → Szenenstapel → DrawList → GX.

#include <gccore.h>

#include <cstdlib>
#include <ctime>

#include "core/theme.hpp"
#include "gfx/draw_list.hpp"
#include "gfx/font_loader.hpp"
#include "gfx/renderer.hpp"
#include "platform/log.hpp"
#include "platform/video.hpp"
#include "platform/wii_input.hpp"
#include "ui/main_scene.hpp"
#include "ui/scene.hpp"
#include "ui/test_library.hpp"

namespace {

constexpr float kFrameMs = 1000.0f / 60.0f;
constexpr int kDarkFrom = 20 * 60, kDarkTo = 7 * 60;  // Standard bis zur Konfiguration in M2

void fillClock(nl::ui::UiContext& ctx) {
    const std::time_t now = std::time(nullptr);
    const std::tm* t = std::localtime(&now);
    if (t == nullptr) return;
    ctx.minuteOfDay = t->tm_hour * 60 + t->tm_min;
    ctx.day = t->tm_mday;
    ctx.month = t->tm_mon + 1;
    ctx.year = t->tm_year + 1900;
    ctx.weekday = (t->tm_wday + 6) % 7;  // tm: 0 = Sonntag → 0 = Montag
}

}  // namespace

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
    auto atlasResult = nl::gfx::buildUiAtlas(renderer.pixelScaleX());
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

    nl::ui::UiContext ctx;
    ctx.width = renderer.logicalWidth();
    ctx.height = renderer.logicalHeight();
    ctx.fonts = &atlas->fonts;
    ctx.shapes = &atlas->shapes;
    ctx.themeMode = nl::ThemeMode::Auto;
    fillClock(ctx);

    nl::ThemeState theme;
    theme.setTarget(nl::isDarkAt(ctx.themeMode, ctx.minuteOfDay, kDarkFrom, kDarkTo), false);

    const nl::ui::Library library = nl::ui::testLibrary();
    nl::ui::MainScene mainScene(library);
    nl::ui::SceneStack scenes;
    scenes.push(&mainScene);

    nl::gfx::DrawList dl(4096, 16);
    bool readyReported = false;
    bool running = true;
    while (running) {
        fillClock(ctx);
        theme.setTarget(nl::isDarkAt(ctx.themeMode, ctx.minuteOfDay, kDarkFrom, kDarkTo), true);
        theme.update(kFrameMs);
        ctx.theme = &theme.current();

        const nl::ui::InputFrame in = mapper.update(input.poll(), kFrameMs);
        const nl::ui::AppCommand cmd = scenes.update(ctx, in, kFrameMs);
        if (cmd == nl::ui::AppCommand::CycleTheme) {
            ctx.themeMode = nl::nextMode(ctx.themeMode);
            nl::log::line("NL_CMD %s %s", nl::ui::appCommandName(cmd), nl::themeModeName(ctx.themeMode));
        }

        dl.clear();
        scenes.draw(ctx, dl);
        renderer.beginFrame(ctx.theme->bg);
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
