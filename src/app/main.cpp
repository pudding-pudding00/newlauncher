// Hauptschleife (M1): Eingabe → Szenenstapel → DrawList → GX.

#include <gccore.h>
#include <ogc/lwp_watchdog.h>

#include <cstdlib>
#include <ctime>

#include "core/theme.hpp"
#include "gfx/draw_list.hpp"
#include "gfx/font_loader.hpp"
#include "gfx/renderer.hpp"
#include "platform/crash.hpp"
#include "platform/log.hpp"
#include "platform/video.hpp"
#include "platform/wii_input.hpp"
#include "ui/debug_overlay.hpp"
#include "ui/home_menu.hpp"
#include "ui/main_scene.hpp"
#include "ui/pointer_cursor.hpp"
#include "ui/scene.hpp"
#include "ui/test_library.hpp"

#ifdef NL_SELFTEST
#include "app/selftest.hpp"
#endif

namespace {

constexpr float kFrameMs = 1000.0f / 60.0f;
constexpr int kDarkFrom = 19 * 60, kDarkTo = 7 * 60;  // Spec 3.4: Standard 19:00-07:00, einstellbar ab M6

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
    nl::platform::installCrashHandler();
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
    ctx.themeMode = nl::ThemeMode::Light;  // Spec 3.4: Standard Hell
    fillClock(ctx);

    nl::ThemeState theme;
    theme.setTarget(nl::isDarkAt(ctx.themeMode, ctx.minuteOfDay, kDarkFrom, kDarkTo), false);

    const nl::ui::Library library = nl::ui::testLibrary();
    nl::ui::MainScene mainScene(library);
    nl::ui::HomeMenu homeMenu;
    mainScene.setHomeMenu(&homeMenu);
    nl::ui::SceneStack scenes;
    scenes.push(&mainScene);

    nl::gfx::DrawList dl(4096, 16);
    nl::ui::DebugOverlay debug;
    nl::FrameStats stats;
    bool readyReported = false;
    bool running = true;
    for (int frame = 0; running; ++frame) {
        static_cast<void>(frame);  // nur im Selbsttest gelesen
        const u64 frameStart = gettime();
        fillClock(ctx);
        theme.setTarget(nl::isDarkAt(ctx.themeMode, ctx.minuteOfDay, kDarkFrom, kDarkTo), true);
        theme.update(kFrameMs);
        ctx.theme = &theme.current();

#ifdef NL_SELFTEST
        const nl::app::SelftestView view{mainScene, scenes, debug};
        input.poll();  // Geräte weiter abfragen, Eingaben kommen aber aus dem Skript
        const nl::ui::InputFrame in = mapper.update(nl::app::selftestInput(frame, view), kFrameMs);
#else
        const nl::ui::InputFrame in = mapper.update(input.poll(), kFrameMs);
#endif
        if (in.pressed(nl::ui::Action::DebugToggle)) debug.toggle();
        const nl::ui::AppCommand cmd = scenes.update(ctx, in, kFrameMs);
        switch (cmd) {
            case nl::ui::AppCommand::CycleTheme:
                ctx.themeMode = nl::nextMode(ctx.themeMode);
                nl::log::line("NL_CMD %s %s", nl::ui::appCommandName(cmd), nl::themeModeName(ctx.themeMode));
                break;
            case nl::ui::AppCommand::ExitToHbc:  // Spec 6.8: aufräumen, exit(0)
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                running = false;
                break;
            case nl::ui::AppCommand::WiiMenu:
            case nl::ui::AppCommand::PowerOff:
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                input.shutdown();
                SYS_ResetSystem(cmd == nl::ui::AppCommand::WiiMenu ? SYS_RETURNTOMENU : SYS_POWEROFF, 0, 0);
                break;
            case nl::ui::AppCommand::None:
                break;
        }
        if (!running) break;

        dl.clear();
        scenes.draw(ctx, dl);
        debug.setStats(stats);
        debug.draw(ctx, dl);
        nl::ui::drawPointerCursor(ctx, dl, in);  // zuletzt: ueber allen Szenen und Overlays
        renderer.beginFrame(ctx.theme->bg);
        renderer.submit(dl);

        // Statistik für den nächsten Frame; CPU-Zeit ohne die VSync-Wartezeit in endFrame.
        stats.cpuMicros = static_cast<u32>(ticks_to_microsecs(diff_ticks(frameStart, gettime())));
        stats.quads = dl.quadCount();
        stats.droppedQuads = dl.droppedQuads();
        stats.textureBytes = renderer.textureBytes();
        renderer.endFrame();
        stats.drawCalls = renderer.drawCallsLastFrame();

        if (!readyReported) {
            nl::log::line("NL_READY");
            readyReported = true;
        }
#ifdef NL_SELFTEST
        nl::app::selftestAfterFrame(frame, view);
#endif
    }
    input.shutdown();
    return 0;
}
