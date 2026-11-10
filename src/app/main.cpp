// Hauptprogramm (M2): Konfiguration → Video → Bibliotheksgerät → Index/Scan → Hauptschleife.

#include <gccore.h>
#include <ogc/lwp_watchdog.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/cover_service.hpp"
#include "core/config.hpp"
#include "core/cover_download.hpp"
#include "core/emulators.hpp"
#include "core/index_file.hpp"
#include "core/launch_layout.hpp"
#include "core/launch_plan.hpp"
#include "core/library_store.hpp"
#include "core/meta_db.hpp"
#include "core/n64.hpp"
#include "core/n64_choice.hpp"
#include "core/theme.hpp"
#include "core/vc_map.hpp"
#include "gfx/draw_list.hpp"
#include "gfx/font_loader.hpp"
#include "gfx/renderer.hpp"
#include "launch/chainloader.hpp"
#include "launch/channel.hpp"
#include "platform/crash.hpp"
#include "platform/log.hpp"
#include "platform/net_worker.hpp"
#include "platform/nand_titles.hpp"
#include "platform/scan_worker.hpp"
#include "platform/std_fs.hpp"
#include "platform/storage.hpp"
#include "platform/video.hpp"
#include "platform/wii_input.hpp"
#include "ui/settings_scene.hpp"
#include "ui/debug_overlay.hpp"
#include "ui/details_scene.hpp"
#include "ui/dialog_scene.hpp"
#include "ui/home_menu.hpp"
#include "ui/jump_bar.hpp"
#include "ui/keyboard.hpp"
#include "ui/launch_text.hpp"
#include "ui/main_scene.hpp"
#include "ui/pointer_cursor.hpp"
#include "ui/scene.hpp"
#include "ui/startup_scene.hpp"
#include "ui/strings.hpp"
#include "ui/vc_map_scene.hpp"

#ifdef NL_SELFTEST
#include "app/selftest.hpp"
#endif

namespace {

constexpr float kFrameMs = 1000.0f / 60.0f;
constexpr const char* kConfigDir = "sd:/newlauncher";
constexpr const char* kConfigPath = "sd:/newlauncher/config.ini";
constexpr const char* kFavoritesPath = "sd:/newlauncher/favorites.ini";
constexpr const char* kHistoryPath = "sd:/newlauncher/history.ini";
constexpr const char* kEmulatorsPath = "sd:/newlauncher/emulators.ini";
constexpr const char* kVcMapPath = "sd:/newlauncher/n64_vc.ini";
constexpr const char* kGamesPath = "sd:/newlauncher/games.ini";
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

std::string readText(nl::FileSystem& fs, const char* path) {
    auto data = nl::readAllWithFallback(fs, path, kMaxIniBytes);
    if (!data.isOk()) return {};
    const auto& bytes = data.value();
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

nl::IniDoc readIni(nl::FileSystem& fs, const char* path) {
    auto data = nl::readAllWithFallback(fs, path, kMaxIniBytes);
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
    auto data = nl::readAllWithFallback(fs, kConfigPath, kMaxIniBytes);
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

nl::EmulatorTable loadEmulators(nl::FileSystem& fs, bool sdOk) {
    if (sdOk) {
        auto data = nl::readAllWithFallback(fs, kEmulatorsPath, kMaxIniBytes);
        if (data.isOk()) {
            const auto& b = data.value();
            nl::log::line("NL_EMULATORS ok");
            return nl::EmulatorTable::fromIni(
                nl::IniDoc::parse(std::string_view(reinterpret_cast<const char*>(b.data()), b.size())));
        }
        if (data.error().code == nl::ErrorCode::NotFound) {
            static_cast<void>(fs.makeDirs(kConfigDir));
            writeText(fs, kEmulatorsPath, nl::defaultEmulatorsText());
        }
    }
    nl::log::line("NL_EMULATORS default");
    return nl::EmulatorTable::fromIni(nl::IniDoc::parse(nl::defaultEmulatorsText()));
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
    // Spec 9: MEM2 ab kDolBufferAddr gehört dem Chainloader. Vor jeder Allokation reservieren.
    const bool launchAllowed = reinterpret_cast<u32>(SYS_GetArena2Hi()) > nl::kDolBufferAddr;
    if (launchAllowed) SYS_SetArena2Hi(reinterpret_cast<void*>(nl::kDolBufferAddr));
    nl::log::init();
    nl::log::line("newlauncher %s", NL_VERSION);
    if (!launchAllowed) nl::log::line("NL_WARN Arena2 schon belegt");

    // 1. SD und Konfiguration (Spec 6.1 Schritt 2)
    nl::platform::StdFileSystem fs;
    const bool sdOk = nl::platform::mountSd();
    nl::Config config = loadConfig(fs, sdOk);  // die Einstellungsszene ändert sie live
    const nl::EmulatorTable emulators = loadEmulators(fs, sdOk);

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

    nl::gfx::DrawList dl(4096, 256);  // Cover und Atlas wechseln sich pro Kachel ab
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
    if (auto data = nl::readAllWithFallback(fs, cacheDir + "/meta.bin", kMaxMetaBytes); data.isOk()) {
        auto parsed = nl::MetaDb::parse(std::move(data).value());
        if (parsed.isOk()) meta.emplace(std::move(parsed).value());
        else nl::log::line("NL_WARN meta.bin verworfen: %s", parsed.error().context.c_str());
    }
    const nl::MetaDb* metaPtr = meta ? &*meta : nullptr;

    nl::LibraryStore store;
    nl::GameOverrides overrides;
    nl::VcMap vcMapping;
    std::string vcMapText;  // Originaltext: Kommentare und unlesbare Einträge überleben das Speichern
    if (sdOk) {
        store.loadFavorites(readIni(fs, kFavoritesPath));
        store.loadHistory(readIni(fs, kHistoryPath));
        overrides = nl::GameOverrides::fromIni(readIni(fs, kGamesPath));
        vcMapText = readText(fs, kVcMapPath);
        vcMapping = nl::VcMap::fromIni(nl::IniDoc::parse(vcMapText));
    }

    // N64-Startwege (Spec 6.6): Kanäle im NAND und die beiden Emulator-DOLs, einmal pro Sitzung.
    const std::vector<u64> installedTitles = nl::platform::installedTitleIds();
    nl::log::line("NL_NAND channels=%u", static_cast<unsigned>(installedTitles.size()));
    const bool not64Dol = nl::fileSize(fs, emulators.forN64(nl::N64Method::Not64)->dol).has_value();
    const bool wii64Dol = nl::fileSize(fs, emulators.forN64(nl::N64Method::Wii64)->dol).has_value();
    auto n64Inputs = [&](const nl::Game& g) {
        nl::N64Inputs in;
        in.override_ = overrides.n64Method(g.key);
        in.mappedTitle = vcMapping.titleFor(g.key);
        in.installedTitles = installedTitles;
        in.not64Dol = not64Dol;
        in.wii64Dol = wii64Dol;
        return in;
    };
    std::vector<nl::ChannelInfo> channels;  // Namen erst beim ersten Öffnen des VC-Dialogs
    bool channelsLoaded = false;
    auto saveIndex = [&](const nl::IndexData& index) {
        static_cast<void>(fs.makeDirs(cacheDir));
        const std::vector<u8> bytes = nl::writeIndex(index);
        if (fs.writeAtomic(indexPath, bytes).isOk()) nl::log::line("NL_INDEX_SAVED");
        else nl::log::line("NL_WARN index.bin nicht gespeichert");
    };

    nl::platform::ScanWorker worker;
    bool haveIndex = false;
    if (auto data = nl::readAllWithFallback(fs, indexPath, kMaxIndexBytes); data.isOk()) {
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
    nl::app::CoverService covers(fs, libRoot);
    nl::ui::MainScene mainScene(libraryView());
    mainScene.setCovers(&covers);
    nl::ui::HomeMenu homeMenu;
    mainScene.setHomeMenu(&homeMenu);
    nl::ui::SettingsScene settings;
    settings.setConfig(&config);
    homeMenu.setSettings(&settings);
    nl::platform::NetWorker net;
    mainScene.setFavoriteKeys(&store);
    nl::ui::JumpBar jumpBar(mainScene);
    mainScene.setJumpBar(&jumpBar);
    nl::ui::Keyboard keyboard(mainScene);
    mainScene.setKeyboard(&keyboard);
    nl::ui::DetailsScene details;
    mainScene.setDetails(&details, &store);
    details.setCovers(&covers);
    nl::ui::DialogScene dialog;
    nl::ui::VcMapScene vcMapScene;
    nl::ui::SceneStack scenes;
    scenes.push(&mainScene);

    // Zeile „Startmethode“ der Detailansicht aus Override, Zuordnung und Verfügbarkeit.
    auto refreshDetailsN64 = [&]() {
        const nl::Game* g = details.game();
        if (g == nullptr || g->console != nl::Console::N64) return;
        details.setN64(nl::decideN64(n64Inputs(*g)), overrides.n64Method(g->key));
    };

    // Spec 9: Preflight, Verlauf, Chainload. Kehrt nur zurück, wenn der Start scheitert (dann mit Dialog).
    auto launchGame = [&](const nl::Game& game) {
        const std::string key = game.key;  // game kann auf Bibliotheksdaten zeigen
        auto showError = [&](const nl::LaunchError& e) {
            char title[64];
            char text[256];
            nl::ui::launchErrorText(e, title, sizeof title, text, sizeof text);
            dialog.open(title, text);
            scenes.push(&dialog);
        };
        const nl::N64Inputs inputs = n64Inputs(game);
        auto target = nl::preflightGame(fs, game, libRoot, emulators, &inputs);
        if (target.isOk() && target.value().kind == nl::LaunchTarget::Kind::Dol && !launchAllowed) {
            target = nl::Result<nl::LaunchTarget, nl::LaunchError>::err(
                {nl::LaunchFailure::NoMemory, nl::ui::tr(nl::ui::Str::NoMemory)});
        }
        if (!target.isOk()) {
            nl::log::line("NL_PREFLIGHT_FAIL %s %s", nl::launchFailureName(target.error().kind), key.c_str());
            showError(target.error());
            return;
        }
        if (target.value().n64) nl::log::line("NL_N64 %s %s", key.c_str(), nl::n64MethodKey(*target.value().n64));
        store.recordPlayed(key, static_cast<u32>(std::time(nullptr)));
        if (sdOk && writeText(fs, kHistoryPath, store.historyIni().serialize())) nl::log::line("NL_HISTORY_SAVED");
        else nl::log::line("NL_WARN history.ini nicht gespeichert");
        nl::log::line("NL_LAUNCH %s", key.c_str());
        worker.cancelAndJoin();  // der Scan liest sonst parallel von der Karte
        net.join();
        covers.shutdown();

        if (target.value().kind == nl::LaunchTarget::Kind::Channel) {
            const u64 titleId = target.value().titleId;
            nl::log::line("NL_LAUNCH_VC %s", nl::formatTitleId(titleId).c_str());
            if (sdOk) nl::log::flushLogFile(fs, kLogPath);
            input.shutdown();
            nl::platform::unmountAll();
            nl::platform::shutdownVideo();
            const nl::Result<void> r = nl::launch::launchChannel(titleId);
            nl::log::line("NL_ERROR Kanalstart: %s", r.error().context.c_str());
            SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
            return;
        }

        const nl::LaunchPlan& plan = target.value().dol;
        if (auto loaded = nl::launch::loadDolToBuffer(fs, plan); !loaded.isOk()) {
            nl::log::line("NL_WARN DOL nicht geladen: %s", loaded.error().context.c_str());
            showError({nl::LaunchFailure::EmuInvalid, plan.dolPath});
            return;
        }
        if (sdOk) nl::log::flushLogFile(fs, kLogPath);
        input.shutdown();
        nl::platform::unmountAll();
        nl::platform::shutdownVideo();
        nl::launch::chainload(plan);
    };

    // Spec 6.4: „Fehlende Cover laden“ (Wii/GC ohne covers_src-PNG und .tex) über den Netz-Thread.
    char coverToast[48] = "";
    u32 coverToastDone = ~0u;
    int coverToastFrames = 0;
    auto fetchCovers = [&]() {
        nl::log::line("NL_CMD %s", nl::ui::appCommandName(nl::ui::AppCommand::FetchCovers));
        if (net.running()) return;
        std::vector<const nl::Game*> games;
        games.reserve(store.games().size());
        for (const nl::Game& g : store.games()) games.push_back(&g);
        const auto present = nl::existingCoverFiles(fs, libRoot);  // vier Ordnerlisten statt zwei je Spiel
        std::vector<nl::CoverJob> jobs =
            nl::coverDownloadJobs(games, [&](const std::string& path) { return present.count(path) > 0; }, libRoot);
        nl::log::line("NL_COVER_DL_START jobs=%u", static_cast<unsigned>(jobs.size()));
        if (jobs.empty()) {
            dialog.open(nl::ui::tr(nl::ui::Str::NoMissingCovers), nl::ui::tr(nl::ui::Str::NoMissingCoversText));
            scenes.push(&dialog);
            return;
        }
        std::vector<std::string> regions;
        for (std::string_view r : nl::coverRegions(config.coverRegion, nl::platform::systemCoverRegion()))
            regions.emplace_back(r);
        const std::string base =
            config.debugCoverUrl.empty() ? std::string(nl::kGametdbCoverBase) : config.debugCoverUrl;
        if (!net.start(fs, libRoot, std::move(jobs), std::move(regions), base)) {
            nl::log::line("NL_WARN Netz-Thread startet nicht");
            return;
        }
        coverToastDone = ~0u;
    };
    // Fortschritt, Marker und Ergebnis des Netz-Threads übernehmen (Haupt-Thread).
    auto pumpNet = [&]() {
        for (const std::string& line : net.takeLogLines()) nl::log::line("%s", line.c_str());
        for (const std::string& stem : net.takeFinishedStems()) covers.invalidate(stem);
        if (net.running()) {
            const u32 done = net.done();
            if (done != coverToastDone) {  // Text nur bei Änderung neu bauen
                std::snprintf(coverToast, sizeof coverToast, nl::ui::tr(nl::ui::Str::CoverProgress), static_cast<unsigned>(done),
                              static_cast<unsigned>(net.total()));
                coverToastDone = done;
                coverToastFrames = 0;
            }
            if (coverToastFrames-- <= 0) {  // Toast hält 2 s; vorher auffrischen
                mainScene.showToast(coverToast);
                coverToastFrames = 90;
            }
        }
        // Die Zusammenfassung wartet, solange der Dialog etwas anderes zeigt (z. B. einen Startfehler).
        if (auto sum = scenes.contains(&dialog) ? std::nullopt : net.take()) {
            for (const std::string& line : net.takeLogLines()) nl::log::line("%s", line.c_str());
            for (const std::string& stem : net.takeFinishedStems()) covers.invalidate(stem);
            nl::log::line("NL_COVER_DL_DONE ok=%u missing=%u failed=%u", static_cast<unsigned>(sum->ok),
                          static_cast<unsigned>(sum->missing), static_cast<unsigned>(sum->failed));
            if (sdOk) nl::log::flushLogFile(fs, kLogPath);
            char text[128];
            std::snprintf(text, sizeof text, nl::ui::tr(nl::ui::Str::CoversSummary), static_cast<unsigned>(sum->ok),
                          static_cast<unsigned>(sum->missing), static_cast<unsigned>(sum->failed));
            dialog.open(nl::ui::tr(nl::ui::Str::CoversLoaded), text);
            scenes.push(&dialog);
        }
    };

    nl::ui::DebugOverlay debug;
    nl::FrameStats stats;
    bool readyReported = false;
    bool coversReported = false;
    bool running = true;
    for (int frameNo = 0; running; ++frameNo) {
        static_cast<void>(frameNo);  // nur im Selbsttest gelesen
        const u64 frameStart = gettime();
#ifdef NL_SELFTEST
        const nl::app::SelftestView view{mainScene, scenes, debug, worker.running(), store, ctx, keyboard, &dialog,
                                                 &details, &vcMapScene, &settings, &covers};
        input.poll();  // Geräte weiter abfragen, Eingaben kommen aber aus dem Skript
        const nl::ui::InputFrame in = frame.begin(nl::app::selftestInput(frameNo, view));
#else
        const nl::ui::InputFrame in = frame.begin(input.poll());
#endif
        // Ergebnis des Hintergrund-Scans übernehmen (Spec 6.1 Schritt 6)
        if (auto result = worker.take()) {
            if (result->isOk()) {
                const nl::ScanStats st = result->value().stats;
                const std::string focusKey = mainScene.focusedKey();  // vor setGames: danach sind die Zeiger ungültig
                store.setGames(std::move(*result).value().index, metaPtr);
                covers.libraryChanged();
                mainScene.setLibrary(libraryView(), focusKey);
                nl::log::line("NL_SCAN_DONE games=%u added=%u removed=%u", static_cast<unsigned>(store.games().size()),
                              static_cast<unsigned>(st.added), static_cast<unsigned>(st.removed));
                saveIndex(store.index());
            } else {
                nl::log::line("NL_WARN Scan fehlgeschlagen: %s", result->error().context.c_str());
            }
        }

        if (in.pressed(nl::ui::Action::DebugToggle)) debug.toggle();
        covers.beginFrame(static_cast<u32>(frameNo) + 1, static_cast<float>(frameNo) * kFrameMs);
        nl::ui::SceneRequests req;
        const nl::ui::AppCommand cmd = scenes.update(ctx, in, kFrameMs, &req);
        if (req.push == &details) refreshDetailsN64();  // MainScene hat die Details gerade geöffnet
        switch (cmd) {
            case nl::ui::AppCommand::SetN64Method: {
                if (req.game == nullptr) break;
                const std::string key = req.game->key;
                const std::optional<nl::N64Method> method =
                    req.n64Choice == 0 ? std::nullopt : std::optional<nl::N64Method>(static_cast<nl::N64Method>(req.n64Choice - 1));
                overrides.setN64Method(key, method);
                if (sdOk && writeText(fs, kGamesPath, overrides.toIni().serialize()))
                    nl::log::line("NL_OVERRIDE_SAVED %s %s", key.c_str(), method ? nl::n64MethodKey(*method) : "auto");
                else nl::log::line("NL_WARN games.ini nicht gespeichert");
                refreshDetailsN64();
                break;
            }
            case nl::ui::AppCommand::OpenVcMap: {
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                if (!channelsLoaded) {
                    channels = nl::platform::loadChannelNames(installedTitles, nl::platform::systemBannerLang());
                    channelsLoaded = true;
                    nl::log::line("NL_VC_CHANNELS n=%u", static_cast<unsigned>(channels.size()));
                    for (const nl::ChannelInfo& c : channels)
                        nl::log::line("NL_VC_CHANNEL %s %s", nl::formatTitleId(c.titleId).c_str(), nl::channelDisplayName(c).c_str());
                }
                std::vector<nl::ui::VcMapRow> rows;
                for (const nl::Game& g : store.games()) {
                    if (g.console != nl::Console::N64) continue;
                    nl::ui::VcMapRow row{g.key, g.title != nullptr ? g.title : g.key, vcMapping.titleFor(g.key), std::nullopt};
                    if (!row.mapped) row.suggestion = nl::suggestChannel(row.title, channels);
                    rows.push_back(std::move(row));
                }
                vcMapScene.open(std::move(rows), channels);
                scenes.push(&vcMapScene);  // wartet, bis das HOME-Menü ausgeblendet ist
                break;
            }
            case nl::ui::AppCommand::SaveVcMap: {
                for (const nl::ui::VcMapRow& row : vcMapScene.rows()) vcMapping.set(row.key, row.mapped);
                std::string text = vcMapping.serialize(vcMapText);
                if (sdOk && writeText(fs, kVcMapPath, text)) {
                    vcMapText = std::move(text);
                    nl::log::line("NL_VCMAP_SAVED n=%u", static_cast<unsigned>(vcMapping.size()));
                } else nl::log::line("NL_WARN n64_vc.ini nicht gespeichert");
                refreshDetailsN64();
                break;
            }
            case nl::ui::AppCommand::ToggleFavorite: {
                if (req.game == nullptr) break;
                const std::string key = req.game->key;
                const std::string focusKey = mainScene.focusedKey();  // vor toggleFavorite: Favoritenliste wird neu gebaut
                const bool nowFavorite = store.toggleFavorite(key);
                const std::string text = store.favoritesIni().serialize();
                if (sdOk && writeText(fs, kFavoritesPath, text)) nl::log::line("NL_FAV_SAVED %s", key.c_str());
                else nl::log::line("NL_WARN favorites.ini nicht gespeichert");
                mainScene.setLibrary(libraryView(), focusKey);
                mainScene.showToast(nowFavorite ? nl::ui::tr(nl::ui::Str::FavAdded) : nl::ui::tr(nl::ui::Str::FavRemoved));
                if (details.game() != nullptr && key == details.game()->key) details.setFavorite(nowFavorite);
                break;
            }
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
                worker.cancelAndJoin();  // erst den Scan beenden, dann Laufwerke aushängen
                net.join();
                covers.shutdown();
                if (sdOk) nl::log::flushLogFile(fs, kLogPath);
                input.shutdown();
                nl::platform::unmountAll();
                SYS_ResetSystem(cmd == nl::ui::AppCommand::WiiMenu ? SYS_RETURNTOMENU : SYS_POWEROFF, 0, 0);
                break;
            case nl::ui::AppCommand::Launch:
                if (req.game != nullptr) launchGame(*req.game);
                break;
            case nl::ui::AppCommand::FetchCovers:
                fetchCovers();
                break;
            case nl::ui::AppCommand::None:
                break;
        }
        if (!running) break;
        pumpNet();
        covers.pump();  // nach update (Wünsche dieses Frames), vor draw; Freigaben erst nach GX_DrawDone des Vorframes

        dl.clear();
        scenes.draw(ctx, dl);
        debug.setStats(stats);
        debug.draw(ctx, dl);
        nl::ui::drawPointerCursor(ctx, dl, in);  // zuletzt: über allen Szenen und Overlays
        renderer.beginFrame(ctx.theme->bg);
        renderer.submit(dl, &covers);

        // Statistik für den nächsten Frame; CPU-Zeit ohne die VSync-Wartezeit in endFrame.
        stats.cpuMicros = static_cast<u32>(ticks_to_microsecs(diff_ticks(frameStart, gettime())));
        stats.quads = dl.quadCount();
        stats.droppedQuads = dl.droppedQuads();
        stats.textureBytes = renderer.textureBytes() + covers.bytesUsed();
        renderer.endFrame();
        stats.drawCalls = renderer.drawCallsLastFrame();

        if (!readyReported) {
            nl::log::line("NL_READY");
            if (sdOk) nl::log::flushLogFile(fs, kLogPath);
            readyReported = true;
            if (const std::string& key = config.debugAutostart; !key.empty()) {  // nur für Tests
                const nl::Game* found = nullptr;
                for (const nl::Game& g : store.games()) {
                    if (key == g.key) found = &g;
                }
                if (found != nullptr) {
                    nl::log::line("NL_AUTOSTART %s", key.c_str());
                    launchGame(*found);
                } else {
                    nl::log::line("NL_WARN autostart %s fehlt", key.c_str());
                }
            }
            if (config.debugFetchCovers) fetchCovers();  // nur für Tests
        }
        if (readyReported && !coversReported && covers.idle()) {
            nl::log::line("NL_COVERS ready=%u", static_cast<unsigned>(covers.readyCount()));
            if (sdOk) nl::log::flushLogFile(fs, kLogPath);
            coversReported = true;
        }
#ifdef NL_SELFTEST
        nl::app::selftestAfterFrame(frameNo, view);
#endif
    }
    worker.cancelAndJoin();  // erst den Scan beenden, dann Laufwerke aushängen
    net.join();
    covers.shutdown();
    if (sdOk) nl::log::flushLogFile(fs, kLogPath);
    input.shutdown();
    nl::platform::unmountAll();
    return 0;
}
