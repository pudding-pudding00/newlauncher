// Hauptprogramm (M2): Konfiguration → Video → Bibliotheksgerät → Index/Scan → Hauptschleife.

#include <gccore.h>
#include <ogc/lwp_watchdog.h>

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <malloc.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/cover_service.hpp"
#include "core/config.hpp"
#include "core/cover_download.hpp"
#include "core/emu_catalog.hpp"
#include "core/emu_install.hpp"
#include "core/emulators.hpp"
#include "core/index_file.hpp"
#include "core/launch_layout.hpp"
#include "core/launch_plan.hpp"
#include "core/playlist.hpp"
#include "core/library_store.hpp"
#include "core/meta_db.hpp"
#include "core/n64.hpp"
#include "core/n64_choice.hpp"
#include "core/perf_stats.hpp"
#include "core/theme.hpp"
#include "core/vc_map.hpp"
#include "gfx/draw_list.hpp"
#include "gfx/font_loader.hpp"
#include "gfx/renderer.hpp"
#include "launch/chainloader.hpp"
#include "launch/channel.hpp"
#include "launch/wii_boot.hpp"
#include "platform/crash.hpp"
#include "platform/emu_worker.hpp"
#include "platform/audio.hpp"
#include "platform/log.hpp"
#include "platform/music.hpp"
#include "platform/http_client.hpp"
#include "platform/net_service.hpp"
#include "platform/net_worker.hpp"
#include "platform/nand_titles.hpp"
#include "platform/scan_worker.hpp"
#include "platform/std_fs.hpp"
#include "platform/storage.hpp"
#include "platform/system.hpp"
#include "platform/video.hpp"
#include "platform/wii_disc.hpp"
#include "platform/wii_input.hpp"
#include "ui/settings_scene.hpp"
#include "ui/debug_overlay.hpp"
#include "ui/details_scene.hpp"
#include "ui/dialog_scene.hpp"
#include "ui/emulators_scene.hpp"
#include "ui/about_scene.hpp"
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
#ifdef NL_TLS
constexpr bool kHasTls = true;  // Standardquellen über HTTPS (M11)
#else
constexpr bool kHasTls = false;
#endif
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

// text erhält den gelesenen bzw. neu geschriebenen Inhalt (Grundlage für configToIniText).
nl::Config loadConfig(nl::FileSystem& fs, bool sdOk, std::string& text) {
    text = nl::defaultConfigText(kHasTls);
    if (!sdOk) {
        nl::log::line("NL_CONFIG default (keine SD)");
        return nl::Config{};
    }
    auto data = nl::readAllWithFallback(fs, kConfigPath, kMaxIniBytes);
    if (data.isOk()) {
        const auto& b = data.value();
        nl::log::line("NL_CONFIG ok");
        text.assign(reinterpret_cast<const char*>(b.data()), b.size());
        return nl::configFromIni(nl::IniDoc::parse(text), kHasTls);
    }
    static_cast<void>(fs.makeDirs(kConfigDir));
    writeText(fs, kConfigPath, nl::defaultConfigText(kHasTls));
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

u32 msSince(u64 start) { return static_cast<u32>(ticks_to_millisecs(diff_ticks(start, gettime()))); }

}  // namespace

int main(int, char**) {
    const u64 bootTicks = gettime();  // Perf: Startzeiten relativ zum Einsprung
    // Spec 9: MEM2 ab kDolBufferAddr gehört dem Chainloader. Vor jeder Allokation reservieren.
    const bool launchAllowed = reinterpret_cast<u32>(SYS_GetArena2Hi()) > nl::kDolBufferAddr;
    if (launchAllowed) SYS_SetArena2Hi(reinterpret_cast<void*>(nl::kDolBufferAddr));
    nl::log::init();
    nl::log::line("newlauncher %s", NL_VERSION);
    if (!launchAllowed) nl::log::line("NL_WARN Arena2 schon belegt");

    // 1. SD und Konfiguration (Spec 6.1 Schritt 2)
    nl::platform::StdFileSystem fs;
    const bool sdOk = nl::platform::mountSd();
    std::string configText;
    nl::Config config = loadConfig(fs, sdOk, configText);  // die Einstellungsszene ändert sie live
    const bool systemGerman = CONF_GetLanguage() == CONF_LANG_GERMAN;
    nl::ui::setLanguage(nl::ui::resolveLanguage(config.language, systemGerman));
    nl::log::line("NL_LANG %s", nl::ui::langKey(nl::ui::language()));
    const bool ahbprotOff = nl::platform::ahbprotDisabled();
    nl::log::line("NL_AHBPROT %s", ahbprotOff ? "off" : "on");
    auto saveConfig = [&]() {
        std::string text = nl::configToIniText(config, configText);
        if (sdOk && writeText(fs, kConfigPath, text)) {
            configText = std::move(text);
            nl::log::line("NL_CONFIG_SAVED");
        } else {
            nl::log::line("NL_WARN config.ini nicht gespeichert");
        }
    };
    const nl::EmulatorTable emulators = loadEmulators(fs, sdOk);
    if (sdOk) {  // Reste einer abgebrochenen Emulator-Installation (Spec 6.9)
        nl::recoverEmuInstall(fs);
        nl::clearEmuTmp(fs);
    }

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
    nl::platform::Audio audio;
    audio.init();
    audio.setSounds(config.sounds);
    nl::platform::Music music;  // startet nach NL_READY, Stimme 0
    music.setVolume(config.musicVolume);
    auto pumpMusic = [&]() {
        for (const std::string& line : music.takeLogLines()) nl::log::line("%s", line.c_str());
    };
    auto startMusic = [&]() {
        if (music.started() || !sdOk) return;
        music.start(nl::musicFiles(fs, "sd:/newlauncher/music"), static_cast<u32>(gettime()));
    };
    // Vor jedem Verlassen des Launchers: erst den Musik-Thread (er speist Stimme 0), dann ASND.
    auto stopAudio = [&]() {
        music.stop();
        pumpMusic();
        audio.shutdown();
    };
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

    // Perf (M7, [debug] perf): nur bei Bedarf angelegt; der Ring hält 2 × 4 KiB.
    std::unique_ptr<nl::PerfStats> perf;
    if (config.debugPerf) perf = std::make_unique<nl::PerfStats>(video.refreshHz());
    u32 firstFrameMs = 0;
    u32 usbWaitMs = 0;
    auto markFirstFrame = [&]() {
        if (perf && firstFrameMs == 0) firstFrameMs = std::max<u32>(1, msSince(bootTicks));
    };
    auto logPerfSummary = [&]() {
        if (!perf) return;
        char line[400];
        perf->formatSummary(line, sizeof line, SYS_GetArena1Size(), SYS_GetArena2Size(),
                            static_cast<u32>(mallinfo().fordblks));
        nl::log::line("%s", line);
    };

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
        markFirstFrame();
    };

    // 3. Bibliotheksgerät (Spec 6.1 Schritt 4)
    std::string libRoot = "sd:";
    if (config.library == nl::Config::LibraryDevice::Usb) {
        nl::platform::UsbMount usb;
        const u64 usbStart = gettime();  // Echtzeit: auch Dialog, blockierender Mount und 50-Hz-Frames
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
        usbWaitMs = msSince(usbStart);
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
    // Nicht const: nach einer Emulator-Installation (Spec 6.9) neu bestimmt.
    bool not64Dol = false, wii64Dol = false;
    auto probeN64Dols = [&]() {
        not64Dol = nl::fileSize(fs, emulators.forN64(nl::N64Method::Not64)->dol).has_value();
        wii64Dol = nl::fileSize(fs, emulators.forN64(nl::N64Method::Wii64)->dol).has_value();
    };
    probeN64Dols();
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
    covers.setPerf(perf.get());
    nl::ui::HomeMenu homeMenu;
    mainScene.setHomeMenu(&homeMenu);
    nl::ui::SettingsScene settings;
    settings.setConfig(&config);
    settings.setActiveLibrary(libRoot == "usb:" ? nl::Config::LibraryDevice::Usb : nl::Config::LibraryDevice::Sd);
    homeMenu.setSettings(&settings);
    nl::platform::setTlsCaPath(config.debugTlsCa);  // vor dem ersten submit; geladen beim ersten HTTPS-Auftrag
    nl::platform::setNetPerf(config.debugPerf);
    nl::platform::NetService netService;  // ein Netz-Thread für Cover und Emulatoren (M11); lebt länger als die Worker
    nl::platform::NetWorker net(netService);
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
    nl::platform::EmuWorker emu(netService);
    nl::ui::EmulatorsScene emuScene;
    nl::ui::AboutScene aboutScene;
#if defined(NL_TLS) && NL_TLS  // ab M11 Task 4 vom CMake-Schalter gesetzt
    aboutScene.setBuildInfo(NL_VERSION, true);
#else
    aboutScene.setBuildInfo(NL_VERSION, false);
#endif
    std::vector<nl::OscApp> emuList;  // zuletzt geladene OSC-Liste; leer bis „Nach Updates suchen“
    const std::string emuRepo = config.debugEmuRepo.empty() ? config.emulatorRepo : config.debugEmuRepo;
    nl::ui::SceneStack scenes;
    scenes.push(&mainScene);

    // Zeile „Startmethode“ der Detailansicht aus Override, Zuordnung und Verfügbarkeit.
    auto refreshDetailsN64 = [&]() {
        const nl::Game* g = details.game();
        if (g == nullptr || g->console != nl::Console::N64) return;
        details.setN64(nl::decideN64(n64Inputs(*g)), overrides.n64Method(g->key));
    };

    // Spec 9: Preflight, Verlauf, Chainload. Kehrt nur zurück, wenn der Start scheitert (dann mit Dialog).
    // Wii-Spiele (M9 Task 5): cIOS prüfen, Fragmentliste rechnen und gegenprüfen, dann preflightWii.
    auto prepareWii = [&](const nl::Game& game) {
        const nl::CiosStatus cios = nl::platform::readCiosStatus();
        switch (cios.state) {
            case nl::CiosState::D2x: nl::log::line("NL_CIOS ok base=%u ver=%s", static_cast<unsigned>(cios.base), cios.version.c_str()); break;
            case nl::CiosState::Unverified: nl::log::line("NL_CIOS unverified"); break;
            case nl::CiosState::Missing: nl::log::line("NL_CIOS missing"); break;
            case nl::CiosState::NotD2x: nl::log::line("NL_CIOS not_d2x %s", cios.version.c_str()); break;
        }
        u32 partLba = 0;
        auto frags = nl::platform::computeWiiFrags(fs, game, libRoot, &partLba);
        if (frags.isOk()) {
            const nl::FragList& f = frags.value();
            const auto bytes = nl::serializeFragList(f);
            char first[48] = "-";
            if (!f.entries.empty()) {
                std::snprintf(first, sizeof first, "%u:%u:%u", static_cast<unsigned>(f.entries[0].offset),
                              static_cast<unsigned>(f.entries[0].sector), static_cast<unsigned>(f.entries[0].count));
            }
            nl::log::line("NL_FRAG dev=%s part_lba=%u n=%u size=%u crc=%08x first=%s", libRoot == "usb:" ? "usb" : "sd",
                          static_cast<unsigned>(partLba), static_cast<unsigned>(f.entries.size()), static_cast<unsigned>(f.size),
                          static_cast<unsigned>(nl::fragListCrc(bytes)), first);
        }
        return nl::preflightWii(game, libRoot, cios, frags, config.debugWiiDryRun);
    };

    auto launchGame = [&](const nl::Game& game) {
        const std::string key = game.key;  // game kann auf Bibliotheksdaten zeigen
        auto showError = [&](const nl::LaunchError& e) {
            char title[64];
            char text[256];
            nl::ui::launchErrorText(e, title, sizeof title, text, sizeof text);
            dialog.open(title, text);
            scenes.push(&dialog);
            audio.play(nl::UiSound::Error);
        };
        const nl::N64Inputs inputs = n64Inputs(game);
#ifdef NL_SELFTEST
        const bool hwAccess = false;  // Der Selbsttest chainloadet nie: der GC-Start endet im Dialog „Kein Hardwarezugriff“
#else
        const bool hwAccess = ahbprotOff || config.debugIgnoreAhbprot;
#endif
        auto target = nl::preflightGame(fs, game, libRoot, emulators, &inputs, hwAccess);
        if (target.isOk() && target.value().kind == nl::LaunchTarget::Kind::Wii) {
            if (auto plan = prepareWii(game); plan.isOk()) {
                if (config.debugWiiDryRun) {
                    nl::log::line("NL_WII_DRYRUN %s", plan.value().id6.c_str());
                    return;
                }
                target.value().wii = std::move(plan.value());
            } else {
                target = nl::Result<nl::LaunchTarget, nl::LaunchError>::err(plan.error());
            }
        }
        if (target.isOk() && target.value().kind != nl::LaunchTarget::Kind::Channel && !launchAllowed) {
            target = nl::Result<nl::LaunchTarget, nl::LaunchError>::err(
                {nl::LaunchFailure::NoMemory, nl::ui::tr(nl::ui::Str::NoMemory)});
        }
        if (!target.isOk()) {
            nl::log::line("NL_PREFLIGHT_FAIL %s %s", nl::launchFailureName(target.error().kind), key.c_str());
            showError(target.error());
            return;
        }
        if (target.value().n64) nl::log::line("NL_N64 %s %s", key.c_str(), nl::n64MethodKey(*target.value().n64));
        if (const auto& nin = target.value().nintendont) {
            const std::string& cfg = target.value().dol.args[1];  // NIN_CFG: GamePath ab 0x14, NUL-terminiert
            nl::log::line("NL_NIN ver=%s path=%s usb=%d", nl::formatNinVersion(*nin).c_str(), cfg.c_str() + 0x14,
                          (static_cast<unsigned char>(cfg[0x0A]) & 0x04) != 0 ? 1 : 0);
        }
        store.recordPlayed(key, static_cast<u32>(std::time(nullptr)));
        if (sdOk && writeText(fs, kHistoryPath, store.historyIni().serialize())) nl::log::line("NL_HISTORY_SAVED");
        else nl::log::line("NL_WARN history.ini nicht gespeichert");
        nl::log::line("NL_LAUNCH %s", key.c_str());
        worker.cancelAndJoin();  // der Scan liest sonst parallel von der Karte
        net.join();
        emu.join();
        netService.shutdown();  // wtls_shutdown im Netz-Thread, dann Thread beendet
        covers.shutdown();

        if (target.value().kind == nl::LaunchTarget::Kind::Channel) {
            const u64 titleId = target.value().titleId;
            nl::log::line("NL_LAUNCH_VC %s", nl::formatTitleId(titleId).c_str());
            stopAudio();
            logPerfSummary();
            if (sdOk) nl::log::flushLogFile(fs, kLogPath);
            input.shutdown();
            nl::platform::unmountAll();
            nl::platform::shutdownVideo();
            const nl::Result<void> r = nl::launch::launchChannel(titleId);
            nl::log::line("NL_ERROR Kanalstart: %s", r.error().context.c_str());
            SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
            return;
        }

        if (target.value().kind == nl::LaunchTarget::Kind::Wii) {
            auto booter = nl::launch::prepareWiiBoot(*target.value().wii, game.title != nullptr ? game.title : "");
            if (!booter.isOk()) {
                nl::log::line("NL_WARN Game-Booter nicht geladen: %s", booter.error().context.c_str());
                showError({nl::LaunchFailure::EmuInvalid, "game_booter"});
                return;
            }
            stopAudio();
            logPerfSummary();
            if (sdOk) nl::log::flushLogFile(fs, kLogPath);
            input.shutdown();
            nl::platform::unmountAll();
            nl::platform::shutdownVideo();
            nl::launch::bootWiiGame(booter.value());
        }

        const nl::LaunchPlan& plan = target.value().dol;
        if (auto loaded = nl::launch::loadDolToBuffer(fs, plan); !loaded.isOk()) {
            nl::log::line("NL_WARN DOL nicht geladen: %s", loaded.error().context.c_str());
            showError({nl::LaunchFailure::EmuInvalid, plan.dolPath});
            return;
        }
        stopAudio();
        logPerfSummary();
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
        if (net.running()) return;  // Emulator-Aufträge warten in derselben Warteschlange (NetService)
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
            config.debugCoverUrl.empty() ? config.coverBase : config.debugCoverUrl;
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
            if (perf) perf->pause();  // der Flush gehört zur Messung, nicht zum Launcher
            const std::string text = nl::ui::coversSummaryText(sum->ok, sum->missing, sum->failed, sum->lastError);
            dialog.open(nl::ui::tr(nl::ui::Str::CoversLoaded), text.c_str());
            scenes.push(&dialog);
        }
    };

    // Emulator-Verwaltung (Spec 6.9). EmuWorker und NetWorker reihen ihre Aufträge in den NetService ein.
    enum class EmuJob : u8 { None, List, Install };
    EmuJob emuJob = EmuJob::None;
    std::string emuDebugPending = config.debugEmuInstall;  // Komma-Liste; nach dem Laden der Liste installieren
    bool autostartPending = false;                         // debug autostart wartet auf diese Installation
    auto refreshEmuRows = [&]() {
        const std::string text = sdOk ? readText(fs, std::string(nl::kInstalledEmusPath).c_str()) : std::string();
        emuScene.setRows(nl::emuRows(nl::parseInstalledEmus(text),
                                     [&](std::string_view dol) { return nl::fileSize(fs, dol).has_value(); }, emuList));
    };
    auto startEmuList = [&]() {
        if (emu.running()) return;
        if (!emu.startFetchList(emuRepo)) {
            nl::log::line("NL_WARN Emulator-Thread startet nicht");
            return;
        }
        emuJob = EmuJob::List;
        emuScene.setListState(nl::ui::EmulatorsScene::ListState::Loading);
    };
    auto startEmuInstall = [&](std::vector<nl::OscApp> apps) {
        if (apps.empty() || !sdOk || emu.running()) return;
        if (!emu.startInstall(fs, emuRepo, std::move(apps))) {
            nl::log::line("NL_WARN Emulator-Thread startet nicht");
            return;
        }
        emuJob = EmuJob::Install;
    };
    // Debug: Apps aus [debug] emu_install, die in der geladenen Liste stehen.
    auto debugEmuTargets = [&]() {
        std::vector<nl::OscApp> out;
        std::string_view rest = emuDebugPending;
        while (!rest.empty()) {
            const std::size_t comma = rest.find(',');
            const std::string_view name = rest.substr(0, comma);
            rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
            for (const nl::OscApp& a : emuList)
                if (a.name == name) out.push_back(a);
        }
        return out;
    };
    // Marker, Fortschritt und Ergebnisse des Emulator-Threads übernehmen (Haupt-Thread).
    auto pumpEmu = [&](auto&& onDebugInstallDone) {
        for (const std::string& line : emu.takeLogLines()) nl::log::line("%s", line.c_str());
        for (const auto& [name, r] : emu.takeResults()) emuScene.setError(name, r.error);
        if (emu.running()) {
            if (emuJob == EmuJob::Install)
                emuScene.setBusy(nl::ui::EmuProgress{emu.currentName(), emu.step(), emu.done(), emu.total(),
                                                     emu.appIndex(), emu.appCount()});
            return;
        }
        if (emuJob == EmuJob::None) return;
        const EmuJob finished = emuJob;
        emuJob = EmuJob::None;
        for (const std::string& line : emu.takeLogLines()) nl::log::line("%s", line.c_str());
        for (const auto& [name, r] : emu.takeResults()) emuScene.setError(name, r.error);
        if (finished == EmuJob::List) {
            if (auto list = emu.takeList()) {
                emuList = std::move(*list);
                emuScene.setListState(nl::ui::EmulatorsScene::ListState::Loaded);
            } else {
                emuScene.setListState(emu.listFailed() ? nl::ui::EmulatorsScene::ListState::Failed
                                                       : nl::ui::EmulatorsScene::ListState::NotLoaded,
                                      emu.listError());
            }
            refreshEmuRows();
            if (!emuDebugPending.empty()) {
                std::vector<nl::OscApp> targets = debugEmuTargets();
                emuDebugPending.clear();
                nl::log::line("NL_EMU_DEBUG_INSTALL n=%u", static_cast<unsigned>(targets.size()));
                startEmuInstall(std::move(targets));
                if (emuJob != EmuJob::Install) onDebugInstallDone();
            }
        } else {
            emuScene.setBusy(std::nullopt);
            refreshEmuRows();
            probeN64Dols();
            nl::log::line("NL_EMU_DONE not64=%d wii64=%d", not64Dol ? 1 : 0, wii64Dol ? 1 : 0);
            onDebugInstallDone();
        }
        if (sdOk) nl::log::flushLogFile(fs, kLogPath);
        if (perf) perf->pause();
    };

    // debug autostart (nur für Tests); wartet ggf. auf die Debug-Emulator-Installation.
    auto runPendingAutostart = [&]() {
        if (!autostartPending) return;
        autostartPending = false;
        const std::string& key = config.debugAutostart;
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
    };

    nl::ui::DebugOverlay debug;
    nl::FrameStats stats;
    bool readyReported = false;
    bool coversReported = false;
    bool running = true;
    u64 prevFrameStart = 0;
    for (int frameNo = 0; running; ++frameNo) {
        static_cast<void>(frameNo);  // nur im Selbsttest gelesen
        const u64 frameStart = gettime();
        if (perf && prevFrameStart != 0) {
            const float intervalMs = static_cast<float>(ticks_to_microsecs(diff_ticks(prevFrameStart, frameStart))) / 1000.0f;
            perf->frame(intervalMs, worker.running() || net.running() || emu.running() || covers.converting());
            if (perf->windowFull()) {
                char line[160];
                perf->takeWindow(line, sizeof line);
                nl::log::line("%s", line);
            }
        }
        prevFrameStart = frameStart;
#ifdef NL_SELFTEST
        const nl::app::SelftestView view{mainScene, scenes, debug, worker.running(), store, ctx, keyboard, &dialog,
                                                 &details, &vcMapScene, &settings, &config, &covers,
                                                 sdOk ? &fs : nullptr, kLogPath};
        input.poll();  // Geräte weiter abfragen, Eingaben kommen aber aus dem Skript
        const nl::ui::InputFrame in = frame.begin(nl::app::selftestInput(frameNo, view));
#else
        const nl::ui::InputFrame in = frame.begin(input.poll());
#endif
        ctx.battery = input.battery();
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
        // Mit Perf echte Zeit, damit die Cover-Latenz langsame Frames enthält; sonst die feste Taktung,
        // die den Selbsttest reproduzierbar hält.
        covers.beginFrame(static_cast<u32>(frameNo) + 1,
                          perf ? static_cast<float>(msSince(bootTicks)) : static_cast<float>(frameNo) * kFrameMs);
        nl::ui::SceneRequests req;
        const nl::ui::AppCommand cmd = scenes.update(ctx, in, kFrameMs, &req);
        if (req.sound != nl::UiSound::None) {
            audio.play(req.sound);
            if (config.debugLogSounds && config.sounds) nl::log::line("NL_SOUND %s", nl::uiSoundName(req.sound));
        }
        if (req.push == &details) refreshDetailsN64();  // MainScene hat die Details gerade geöffnet
        if (req.push == &homeMenu)
            nl::log::line("NL_BATTERY p1=%d p2=%d p3=%d p4=%d", ctx.battery[0], ctx.battery[1], ctx.battery[2],
                          ctx.battery[3]);
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
                    channels = nl::platform::loadChannelNames(installedTitles, nl::platform::bannerLangFor(nl::ui::language()));
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
                config.themeMode = ctx.themeMode;
                nl::log::line("NL_CMD %s %s", nl::ui::appCommandName(cmd), nl::themeModeName(ctx.themeMode));
                saveConfig();
                break;
            case nl::ui::AppCommand::SettingsChanged: {
                const nl::ui::Lang lang = nl::ui::resolveLanguage(config.language, systemGerman);
                if (lang != nl::ui::language()) {
                    nl::ui::setLanguage(lang);
                    channelsLoaded = false;  // Kanalnamen beim nächsten Öffnen in der neuen Sprache laden
                    nl::log::line("NL_LANG %s", nl::ui::langKey(lang));
                }
                ctx.themeMode = config.themeMode;
                theme.setTarget(nl::isDarkAt(ctx.themeMode, ctx.minuteOfDay, config.darkFromMin, config.darkToMin), true);
                audio.setSounds(config.sounds);
                music.setEnabled(config.music);
                music.setVolume(config.musicVolume);
                if (config.music) startMusic();  // beim Start ausgeschaltet gewesen
                break;
            }
            case nl::ui::AppCommand::SaveSettings:
                config.themeMode = ctx.themeMode;
                saveConfig();
                break;
            case nl::ui::AppCommand::Rescan:
                if (worker.running()) break;  // läuft schon, das Ergebnis kommt über den Scan-Pfad unten
                if (!worker.start(fs, libRoot, nullptr, metaPtr)) {
                    nl::log::line("NL_WARN Neu-Scan nicht gestartet");
                    break;
                }
                mainScene.showToast(nl::ui::tr(nl::ui::Str::Rescanning));
                nl::log::line("NL_RESCAN start");
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
                emu.join();
                netService.shutdown();
                covers.shutdown();
                stopAudio();
                logPerfSummary();
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
            case nl::ui::AppCommand::OpenEmulators:
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                refreshEmuRows();
                scenes.push(&emuScene);
                break;
            case nl::ui::AppCommand::EmuFetchList:
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                startEmuList();
                break;
            case nl::ui::AppCommand::EmuInstall:
            case nl::ui::AppCommand::EmuInstallAll:
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                startEmuInstall(emuScene.takeTargets());
                break;
            case nl::ui::AppCommand::OpenAbout:
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                scenes.push(&aboutScene);
                nl::log::line("NL_SCENE about");
                break;
            case nl::ui::AppCommand::EmuCancel:
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                emu.cancel();
                break;
            case nl::ui::AppCommand::None:
                break;
        }
        if (!running) break;
        pumpNet();
        pumpEmu(runPendingAutostart);
        pumpMusic();
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
        markFirstFrame();

        if (!readyReported) {
            nl::log::line("NL_READY");
            if (perf) {
                perf->setBoot(firstFrameMs, msSince(bootTicks), usbWaitMs);
                char line[160];
                perf->formatBoot(line, sizeof line);
                nl::log::line("%s", line);
            }
            if (sdOk) nl::log::flushLogFile(fs, kLogPath);
            if (perf) perf->pause();  // Flush und Musikstart liegen im nächsten Abstand
            readyReported = true;
            if (config.music) startMusic();
            autostartPending = !config.debugAutostart.empty();
            if (!emuDebugPending.empty()) startEmuList();  // nur für Tests; Autostart wartet auf die Installation
            if (emuJob == EmuJob::None) {
                emuDebugPending.clear();  // Thread startet nicht: nichts zu warten
                runPendingAutostart();
            }
            if (config.debugFetchCovers) fetchCovers();  // nur für Tests
            if (config.debugOpenScene == "about") {   // nur für Tests (M11)
                scenes.push(&aboutScene);
                nl::log::line("NL_SCENE about");
            }
        }
        if (readyReported && !coversReported && covers.idle()) {
            nl::log::line("NL_COVERS ready=%u", static_cast<unsigned>(covers.readyCount()));
            if (sdOk) nl::log::flushLogFile(fs, kLogPath);
            if (perf) perf->pause();
            coversReported = true;
        }
#ifdef NL_SELFTEST
        nl::app::selftestAfterFrame(frameNo, view);
#endif
    }
    worker.cancelAndJoin();  // erst den Scan beenden, dann Laufwerke aushängen
    net.join();
    emu.join();
    netService.shutdown();
    covers.shutdown();
    stopAudio();
    logPerfSummary();
    if (sdOk) nl::log::flushLogFile(fs, kLogPath);
    input.shutdown();
    nl::platform::unmountAll();
    return 0;
}
