// Hauptprogramm (M2): Konfiguration → Video → Bibliotheksgerät → Index/Scan → Hauptschleife.

#include <gccore.h>
#include <ogc/lwp_watchdog.h>

#include <cstdlib>
#include <ctime>
#include <optional>
#include <string>

#include "core/config.hpp"
#include "core/index_file.hpp"
#include "core/library_store.hpp"
#include "core/meta_db.hpp"
#include "core/theme.hpp"
#include "gfx/draw_list.hpp"
#include "gfx/font_loader.hpp"
#include "gfx/renderer.hpp"
#include "platform/crash.hpp"
#include "platform/log.hpp"
#include "platform/scan_worker.hpp"
#include "platform/std_fs.hpp"
#include "platform/storage.hpp"
#include "platform/video.hpp"
#include "platform/wii_input.hpp"
#include "ui/debug_overlay.hpp"
#include "ui/home_menu.hpp"
#include "ui/main_scene.hpp"
#include "ui/pointer_cursor.hpp"
#include "ui/scene.hpp"
#include "ui/startup_scene.hpp"

#ifdef NL_SELFTEST
#include "app/selftest.hpp"
#endif

namespace {

constexpr float kFrameMs = 1000.0f / 60.0f;
constexpr const char* kConfigDir = "sd:/newlauncher";
constexpr const char* kConfigPath = "sd:/newlauncher/config.ini";
constexpr const char* kFavoritesPath = "sd:/newlauncher/favorites.ini";
constexpr const char* kHistoryPath = "sd:/newlauncher/history.ini";
constexpr const char* kLogPath = "sd:/newlauncher/log.txt";
constexpr std::size_t kMaxIniBytes = 256 * 1024;
constexpr std::size_t kMaxIndexBytes = 16 * 1024 * 1024;
constexpr std::size_t kMaxMetaBytes = 8 * 1024 * 1024;

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

nl::IniDoc readIni(nl::FileSystem& fs, const char* path) {
    auto data = fs.readAll(path, kMaxIniBytes);
    if (!data.isOk()) return nl::IniDoc{};
    const auto& bytes = data.value();
    return nl::IniDoc::parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

bool writeText(nl::FileSystem& fs, const char* path, const std::string& text) {
    const auto* p = reinterpret_cast<const u8*>(text.data());
    return fs.writeAtomic(path, std::span<const u8>(p, text.size())).isOk();
}

nl::Config loadConfig(nl::FileSystem& fs, bool sdOk) {
    if (!sdOk) {
        nl::log::line("NL_CONFIG default (keine SD)");
        return nl::Config{};
    }
    auto data = fs.readAll(kConfigPath, kMaxIniBytes);
    if (data.isOk()) {
        const auto& b = data.value();
        nl::log::line("NL_CONFIG ok");
        return nl::configFromIni(nl::IniDoc::parse(std::string_view(reinterpret_cast<const char*>(b.data()), b.size())));
    }
    static_cast<void>(fs.makeDirs(kConfigDir));
    writeText(fs, kConfigPath, nl::defaultConfigText());
    nl::log::line("NL_CONFIG default");
    return nl::Config{};
}

// Ein Frame: Eingabe, Szene(n) aktualisieren, zeichnen, präsentieren. Gemeinsam für Startphase und Hauptschleife.
struct Frame {
    nl::gfx::Renderer& renderer;
    nl::platform::WiiInput& input;
    nl::ui::InputMapper& mapper;
    nl::ui::UiContext& ctx;
    nl::ThemeState& theme;
    nl::gfx::DrawList& dl;
    const nl::Config& config;

    nl::ui::InputFrame begin(const nl::ui::RawInput& raw) {
        fillClock(ctx);
        theme.setTarget(nl::isDarkAt(ctx.themeMode, ctx.minuteOfDay, config.darkFromMin, config.darkToMin), true);
        theme.update(kFrameMs);
        ctx.theme = &theme.current();
        return mapper.update(raw, kFrameMs);
    }
    void present() {
        renderer.beginFrame(ctx.theme->bg);
        renderer.submit(dl);
        renderer.endFrame();
    }
};

}  // namespace

int main(int, char**) {
    nl::log::init();
    nl::log::line("newlauncher %s", NL_VERSION);

    // 1. SD und Konfiguration (Spec 6.1 Schritt 2)
    nl::platform::StdFileSystem fs;
    const bool sdOk = nl::platform::mountSd();
    const nl::Config config = loadConfig(fs, sdOk);

    // 2. Video, Renderer, Atlas, Theme aus der Konfiguration
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
    ctx.themeMode = config.themeMode;
    fillClock(ctx);
    nl::ThemeState theme;
    theme.setTarget(nl::isDarkAt(ctx.themeMode, ctx.minuteOfDay, config.darkFromMin, config.darkToMin), false);
    ctx.theme = &theme.current();

    nl::gfx::DrawList dl(4096, 16);
    Frame frame{renderer, input, mapper, ctx, theme, dl, config};
    nl::ui::StartupScene startup;
    auto startupFrame = [&]() {
        const nl::ui::InputFrame in = frame.begin(input.poll());
        nl::ui::SceneRequests ignored;
        startup.update(ctx, in, kFrameMs, ignored);
        dl.clear();
        startup.draw(ctx, dl);
        nl::ui::drawPointerCursor(ctx, dl, in);
        frame.present();
    };

    // 3. Bibliotheksgerät (Spec 6.1 Schritt 4)
    std::string libRoot = "sd:";
    if (config.library == nl::Config::LibraryDevice::Usb) {
        nl::platform::UsbMount usb;
        usb.begin(config.usbTimeoutSec);
        bool decided = false;
        while (!decided) {
            const auto state = usb.poll(kFrameMs);
            if (state == nl::platform::UsbMount::State::Ready) {
                libRoot = "usb:";
                decided = true;
            } else if (state == nl::platform::UsbMount::State::TimedOut) {
                startup.showUsbFailed();
                startupFrame();
                const auto choice = startup.takeChoice();
                if (choice == nl::ui::StartupScene::Choice::Retry) usb.begin(config.usbTimeoutSec);
                if (choice == nl::ui::StartupScene::Choice::UseSd) decided = true;
                continue;
            } else {
                startup.showWaitingUsb(usb.secondsLeft());
            }
            startupFrame();
        }
    }
    nl::log::line("NL_MOUNT %s", libRoot.c_str());
    const std::string cacheDir = libRoot + "/newlauncher/cache";
    const std::string indexPath = cacheDir + "/index.bin";

    // 4. meta.bin, index.bin, Favoriten, Verlauf (Spec 6.1 Schritt 5)
    std::optional<nl::MetaDb> meta;
    if (auto data = fs.readAll(cacheDir + "/meta.bin", kMaxMetaBytes); data.isOk()) {
        auto parsed = nl::MetaDb::parse(std::move(data).value());
        if (parsed.isOk()) meta.emplace(std::move(parsed).value());
        else nl::log::line("NL_WARN meta.bin verworfen: %s", parsed.error().context.c_str());
    }
    const nl::MetaDb* metaPtr = meta ? &*meta : nullptr;

    nl::LibraryStore store;
    if (sdOk) {
        store.loadFavorites(readIni(fs, kFavoritesPath));
        store.loadHistory(readIni(fs, kHistoryPath));
    }
    auto saveIndex = [&](const nl::IndexData& index) {
        static_cast<void>(fs.makeDirs(cacheDir));
        const std::vector<u8> bytes = nl::writeIndex(index);
        if (fs.writeAtomic(indexPath, bytes).isOk()) nl::log::line("NL_INDEX_SAVED");
        else nl::log::line("NL_WARN index.bin nicht gespeichert");
    };

    nl::platform::ScanWorker worker;
    bool haveIndex = false;
    if (auto data = fs.readAll(indexPath, kMaxIndexBytes); data.isOk()) {
        auto index = nl::readIndex(data.value());
        if (index.isOk()) {
            store.setGames(std::move(index).value(), metaPtr);
            haveIndex = true;
            nl::log::line("NL_LIBRARY games=%u source=index", static_cast<unsigned>(store.games().size()));
            worker.start(fs, libRoot, &store.index(), metaPtr);  // Hintergrund-Scan (Schritt 6)
        } else {
            nl::log::line("NL_WARN index.bin verworfen: %s", index.error().context.c_str());
        }
    }
    if (!haveIndex) {
        worker.start(fs, libRoot, nullptr, metaPtr);
        std::optional<nl::Result<nl::ScanResult>> result;
        while (!(result = worker.take())) {
            startup.showScanning(worker.filesSeen());
            startupFrame();
        }
        if (result->isOk()) {
            const nl::ScanStats st = result->value().stats;
            store.setGames(std::move(*result).value().index, metaPtr);
            nl::log::line("NL_SCAN_DONE games=%u added=%u removed=%u", static_cast<unsigned>(store.games().size()),
                          static_cast<unsigned>(st.added), static_cast<unsigned>(st.removed));
            saveIndex(store.index());
        } else {
            nl::log::line("NL_WARN Scan fehlgeschlagen: %s", result->error().context.c_str());
        }
        nl::log::line("NL_LIBRARY games=%u source=scan", static_cast<unsigned>(store.games().size()));
    }
    auto libraryView = [&]() { return nl::ui::Library{store.games(), store.favorites(), store.recent()}; };

    // 5. Hauptbildschirm
    nl::ui::MainScene mainScene(libraryView());
    nl::ui::HomeMenu homeMenu;
    mainScene.setHomeMenu(&homeMenu);
    nl::ui::SceneStack scenes;
    scenes.push(&mainScene);

    nl::ui::DebugOverlay debug;
    nl::FrameStats stats;
    bool readyReported = false;
    bool running = true;
    for (int frameNo = 0; running; ++frameNo) {
        static_cast<void>(frameNo);  // nur im Selbsttest gelesen
        const u64 frameStart = gettime();
#ifdef NL_SELFTEST
        const nl::app::SelftestView view{mainScene, scenes, debug, worker.running()};
        input.poll();  // Geräte weiter abfragen, Eingaben kommen aber aus dem Skript
        const nl::ui::InputFrame in = frame.begin(nl::app::selftestInput(frameNo, view));
#else
        const nl::ui::InputFrame in = frame.begin(input.poll());
#endif
        // Ergebnis des Hintergrund-Scans übernehmen (Spec 6.1 Schritt 6)
        if (auto result = worker.take()) {
            if (result->isOk()) {
                const nl::ScanStats st = result->value().stats;
                store.setGames(std::move(*result).value().index, metaPtr);
                mainScene.setLibrary(libraryView());
                nl::log::line("NL_SCAN_DONE games=%u added=%u removed=%u", static_cast<unsigned>(store.games().size()),
                              static_cast<unsigned>(st.added), static_cast<unsigned>(st.removed));
                saveIndex(store.index());
            } else {
                nl::log::line("NL_WARN Scan fehlgeschlagen: %s", result->error().context.c_str());
            }
        }

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
                if (sdOk) nl::log::flushLogFile(fs, kLogPath);
                input.shutdown();
                nl::platform::unmountAll();
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
        nl::ui::drawPointerCursor(ctx, dl, in);  // zuletzt: über allen Szenen und Overlays
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
            if (sdOk) nl::log::flushLogFile(fs, kLogPath);
            readyReported = true;
        }
#ifdef NL_SELFTEST
        nl::app::selftestAfterFrame(frameNo, view);
#endif
    }
    if (sdOk) nl::log::flushLogFile(fs, kLogPath);
    input.shutdown();
    nl::platform::unmountAll();
    return 0;
}
