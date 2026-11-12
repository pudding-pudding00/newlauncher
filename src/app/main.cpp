// Hauptprogramm (M2): Konfiguration → Video → Bibliotheksgerät → Index/Scan → Hauptschleife.

#include <gccore.h>
#include <ogc/lwp_watchdog.h>

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <malloc.h>
#include <unistd.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/cover_service.hpp"
#include "core/config.hpp"
#include "core/cover_download.hpp"
#include "core/data_dir.hpp"
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
#ifdef NL_TLS
constexpr bool kHasTls = true;  // Standardquellen über HTTPS (M11)
#else
constexpr bool kHasTls = false;
#endif

// <data> (Spec v2 5.1): Ordner newlauncher/ auf dem Startgerät, sonst SD, sonst USB (core/data_dir).
struct DataDir {
    std::string dir;        // z. B. "usb:/newlauncher"; leer, wenn kein Gerät taugt
    bool readable = false;  // dir ist gesetzt
    bool writable = false;  // sonst läuft der Launcher nur lesend
    std::string path(const char* name) const { return dir + "/" + name; }
};
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
nl::Config loadConfig(nl::FileSystem& fs, const DataDir& dataDir, std::string& text) {
    text = nl::defaultConfigText(kHasTls);
    if (!dataDir.readable) {
        nl::log::line("NL_CONFIG default (kein Datenordner)");
        return nl::Config{};
    }
    const std::string configPath = dataDir.path("config.ini");
    auto data = nl::readAllWithFallback(fs, configPath, kMaxIniBytes);
    if (data.isOk()) {
        const auto& b = data.value();
        nl::log::line("NL_CONFIG ok");
        text.assign(reinterpret_cast<const char*>(b.data()), b.size());
        nl::Config config = nl::configFromIni(nl::IniDoc::parse(text), kHasTls);
        for (const std::string& key : config.libraryRules.bad) nl::log::line("NL_CFG_BAD %s", key.c_str());
        for (const std::string& key : config.badKeys) nl::log::line("NL_CFG_BAD %s", key.c_str());
        return config;
    }
    if (dataDir.writable) writeText(fs, configPath.c_str(), nl::defaultConfigText(kHasTls));
    nl::log::line("NL_CONFIG default");
    return nl::Config{};
}

nl::EmulatorTable loadEmulators(nl::FileSystem& fs, const DataDir& dataDir) {
    if (dataDir.readable) {
        const std::string emulatorsPath = dataDir.path("emulators.ini");
        auto data = nl::readAllWithFallback(fs, emulatorsPath, kMaxIniBytes);
        if (data.isOk()) {
            const auto& b = data.value();
            nl::log::line("NL_EMULATORS ok");
            return nl::EmulatorTable::fromIni(
                nl::IniDoc::parse(std::string_view(reinterpret_cast<const char*>(b.data()), b.size())));
        }
        if (data.error().code == nl::ErrorCode::NotFound && dataDir.writable) {
            writeText(fs, emulatorsPath.c_str(), nl::defaultEmulatorsText());
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

constexpr int kDataUsbTries = 6;           // höchstens ~2,5 s Wartezeit vor der Datenordner-Wahl
constexpr u32 kDataUsbRetryUs = 500 * 1000;

// Hängt bei Start von USB das USB-Gerät sofort ein (der Ordner kann dort liegen), wählt <data> und legt ihn an.
DataDir chooseData(nl::FileSystem& fs, const char* argv0) {
    auto& table = nl::platform::deviceTable();
    auto collect = [&] {
        std::vector<nl::DataCandidate> out;
        for (const nl::platform::Device& d : table.devices()) {
            if (!d.mounted) continue;
            const bool hasDir = fs.list(d.slot.name + ":/newlauncher").isOk();
            out.push_back(nl::DataCandidate{d.slot.name, d.writable, hasDir});
        }
        return out;
    };
    std::vector<nl::DataCandidate> candidates = collect();
    // Ordner kann auf USB liegen (Start von USB oder nichts auf SD): kurz mehrmals versuchen, das Laufwerk
    // braucht nach dem Einschalten etwas. SD-Nutzer mit newlauncher/ warten nie.
    for (int attempt = 0; attempt < kDataUsbTries && nl::wantUsbForData(argv0, candidates); ++attempt) {
        if (attempt > 0) usleep(kDataUsbRetryUs);
        if (table.mountUsb()) candidates = collect();
    }
    const nl::DataDirChoice choice = nl::chooseDataDir(argv0, candidates);
    DataDir out{choice.path, !choice.path.empty(), !choice.path.empty() && !choice.readOnly};
    if (choice.create && !fs.makeDirs(choice.path).isOk()) out = DataDir{};
    nl::log::line("NL_DATA %s%s", out.readable ? out.dir.c_str() : "-", out.writable ? "" : " readonly");
    return out;
}

u32 msSince(u64 start) { return static_cast<u32>(ticks_to_millisecs(diff_ticks(start, gettime()))); }

}  // namespace

int main(int argc, char** argv) {
    const u64 bootTicks = gettime();  // Perf: Startzeiten relativ zum Einsprung
    // Spec 9: MEM2 ab kDolBufferAddr gehört dem Chainloader. Vor jeder Allokation reservieren.
    const bool launchAllowed = reinterpret_cast<u32>(SYS_GetArena2Hi()) > nl::kDolBufferAddr;
    if (launchAllowed) SYS_SetArena2Hi(reinterpret_cast<void*>(nl::kDolBufferAddr));
    nl::log::init();
    nl::log::line("newlauncher %s", NL_VERSION);
    if (!launchAllowed) nl::log::line("NL_WARN Arena2 schon belegt");

    // 1. SD und Konfiguration (Spec 6.1 Schritt 2)
    nl::platform::StdFileSystem fs;
    const bool sdOk = nl::platform::mountSd();  // Emulatoren und ihre Installation bleiben auf der SD (Spec 6.9)
    const DataDir data = chooseData(fs, argc > 0 && argv != nullptr && argv[0] != nullptr ? argv[0] : "");
    const std::string configPath = data.path("config.ini"), favoritesPath = data.path("favorites.ini"),
                      historyPath = data.path("history.ini"), vcMapPath = data.path("n64_vc.ini"),
                      gamesPath = data.path("games.ini"), logPath = data.path("log.txt");
    std::string configText;
    nl::Config config = loadConfig(fs, data, configText);  // die Einstellungsszene ändert sie live
    const bool systemGerman = CONF_GetLanguage() == CONF_LANG_GERMAN;
    nl::ui::setLanguage(nl::ui::resolveLanguage(config.language, systemGerman));
    nl::log::line("NL_LANG %s", nl::ui::langKey(nl::ui::language()));
    const bool ahbprotOff = nl::platform::ahbprotDisabled();
    nl::log::line("NL_AHBPROT %s", ahbprotOff ? "off" : "on");
    auto saveConfig = [&]() {
        std::string text = nl::configToIniText(config, configText);
        if (data.writable && writeText(fs, configPath.c_str(), text)) {
            configText = std::move(text);
            nl::log::line("NL_CONFIG_SAVED");
        } else {
            nl::log::line("NL_WARN config.ini nicht gespeichert");
        }
    };
    const nl::EmulatorTable emulators = loadEmulators(fs, data);
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
        if (music.started() || !data.readable) return;
        music.start(nl::musicFiles(fs, data.path("music")), static_cast<u32>(gettime()));
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

    // 3. Bibliotheksgeräte (Spec 6.1 Schritt 4, Spec v2 5.2): Quellen aus [library] sources.
    // USB wird nur abgewartet, wenn die Quellen USB enthalten können. Bei „auto“ geht es nach der Wartezeit
    // still mit den übrigen Geräten weiter; eine ausdrückliche USB-Quelle zeigt den v1-Dialog.
    const nl::SourceSpec& sources = config.libraryRules.sources;
    bool wantUsb = sources.autoMode;
    for (const std::string& d : sources.devices)
        if (d.rfind("usb", 0) == 0) wantUsb = true;
    if (wantUsb) {
        nl::platform::UsbMount usb;
        const u64 usbStart = gettime();  // Echtzeit: auch Dialog, blockierender Mount und 50-Hz-Frames
        usb.begin(config.usbTimeoutSec);
        bool decided = false;
        while (!decided) {
            const auto state = usb.poll(kFrameMs);
            if (state == nl::platform::UsbMount::State::Ready) {
                decided = true;
            } else if (state == nl::platform::UsbMount::State::TimedOut && sources.autoMode) {
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
    auto& devTable = nl::platform::deviceTable();
    std::vector<std::string> libDevices;
    {
        std::vector<std::string> mounted;
        for (const nl::platform::Device& d : devTable.devices())
            if (d.mounted) mounted.push_back(d.slot.name);
        libDevices = nl::orderedSources(sources, mounted);
    }
    const bool libFellBack = libDevices.empty();
    if (libFellBack) libDevices.push_back("sd");  // v1: Rückfall auf die SD-Karte
    {
        std::string list;
        for (const std::string& d : libDevices) list += (list.empty() ? "" : ",") + d + ":";
        nl::log::line("NL_MOUNT %s", list.c_str());
    }
    // Cover und index.bin liegen auf dem Gerät selbst; ist es nicht beschreibbar, unter <data>/cache/<gerät>.
    auto deviceWritable = [&](const std::string& dev) {
        const nl::platform::Device* d = devTable.byName(dev);
        return d != nullptr && d->writable;
    };
    auto cacheDirOf = [&](const std::string& dev) { return nl::deviceCacheDir(dev, deviceWritable(dev), data.dir); };
    auto coverRootsOf = [&](std::span<const std::string> devices) {
        std::vector<std::string> roots;
        for (const std::string& dev : devices) roots.push_back(nl::coverRoot(dev, deviceWritable(dev), data.dir));
        return roots;
    };

    // 4. meta.bin, index.bin, Favoriten, Verlauf (Spec 6.1 Schritt 5)
    std::optional<nl::MetaDb> meta;  // vom ersten Bibliotheksgerät
    if (auto data = nl::readAllWithFallback(fs, cacheDirOf(libDevices.front()) + "/meta.bin", kMaxMetaBytes); data.isOk()) {
        auto parsed = nl::MetaDb::parse(std::move(data).value());
        if (parsed.isOk()) meta.emplace(std::move(parsed).value());
        else nl::log::line("NL_WARN meta.bin verworfen: %s", parsed.error().context.c_str());
    }
    const nl::MetaDb* metaPtr = meta ? &*meta : nullptr;

    nl::LibraryStore store;
    nl::GameOverrides overrides;
    nl::VcMap vcMapping;
    std::string vcMapText;  // Originaltext: Kommentare und unlesbare Einträge überleben das Speichern
    if (data.readable) {
        store.loadFavorites(readIni(fs, favoritesPath.c_str()));
        store.loadHistory(readIni(fs, historyPath.c_str()));
        overrides = nl::GameOverrides::fromIni(readIni(fs, gamesPath.c_str()));
        for (const std::string& key : overrides.badKeys()) nl::log::line("NL_OPT_BAD %s", key.c_str());
        vcMapText = readText(fs, vcMapPath.c_str());
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
    // Ein Index je Gerät (Spec v2 5.2); store hält die zusammengeführte Sicht.
    std::vector<nl::DeviceIndex> deviceIndexes;
    auto saveIndexes = [&]() {
        bool ok = true;
        for (const nl::DeviceIndex& di : deviceIndexes) {
            const std::string dir = cacheDirOf(di.device);
            static_cast<void>(fs.makeDirs(dir));
            ok = fs.writeAtomic(dir + "/index.bin", nl::writeIndex(di.index)).isOk() && ok;
        }
        if (ok) nl::log::line("NL_INDEX_SAVED");
        else nl::log::line("NL_WARN index.bin nicht gespeichert");
    };
    auto logGames = [&]() {
        if (!config.debugListGames) return;
        for (const nl::Game& g : store.games())
            nl::log::line("NL_GAME %s %s", g.key, store.devices()[g.device].c_str());
    };
    auto applyIndexes = [&]() {
        store.setGames(nl::mergeIndexes(deviceIndexes), metaPtr);
        logGames();
    };

    nl::platform::ScanWorker worker;
    nl::ScanRules scanRules = nl::defaultScanRules();
    scanRules.paths = config.libraryRules.paths;  // [paths]/[extensions] aus config.ini (M12)
    for (const std::string& dev : libDevices) {
        auto bytes = nl::readAllWithFallback(fs, cacheDirOf(dev) + "/index.bin", kMaxIndexBytes);
        if (!bytes.isOk()) continue;
        auto index = nl::readIndex(bytes.value());
        if (index.isOk()) deviceIndexes.push_back(nl::DeviceIndex{dev, std::move(index).value()});
        else nl::log::line("NL_WARN index.bin verworfen (%s): %s", dev.c_str(), index.error().context.c_str());
    }
    if (!deviceIndexes.empty()) {
        applyIndexes();
        nl::log::line("NL_LIBRARY games=%u source=index", static_cast<unsigned>(store.games().size()));
        worker.start(fs, libDevices, scanRules, deviceIndexes, metaPtr);  // Hintergrund-Scan (Schritt 6)
    } else {
        worker.start(fs, libDevices, scanRules, {}, metaPtr);
        std::optional<nl::Result<nl::MultiScan>> result;
        while (!(result = worker.take())) {
            startup.showScanning(worker.filesSeen());
            startupFrame();
        }
        if (result->isOk()) {
            const nl::ScanStats st = result->value().stats;
            deviceIndexes = std::move(*result).value().indexes;
            applyIndexes();
            nl::log::line("NL_SCAN_DONE games=%u added=%u removed=%u", static_cast<unsigned>(store.games().size()),
                          static_cast<unsigned>(st.added), static_cast<unsigned>(st.removed));
            saveIndexes();
        } else {
            nl::log::line("NL_WARN Scan fehlgeschlagen: %s", result->error().context.c_str());
        }
        nl::log::line("NL_LIBRARY games=%u source=scan", static_cast<unsigned>(store.games().size()));
    }
    auto libraryView = [&]() { return nl::ui::Library{store.games(), store.favorites(), store.recent()}; };

    // 5. Hauptbildschirm
    nl::app::CoverService covers(fs, coverRootsOf(store.devices()));
    nl::ui::MainScene mainScene(libraryView());
    mainScene.setCovers(&covers);
    covers.setPerf(perf.get());
    nl::ui::HomeMenu homeMenu;
    mainScene.setHomeMenu(&homeMenu);
    nl::ui::SettingsScene settings;
    settings.setConfig(&config);
    settings.setActiveSources(libFellBack ? nl::SourceSpec{false, {"sd"}} : sources);
    // Wurzel des Geräts, auf dem ein Spiel liegt ("usb2:"); Game::device zählt in store.devices().
    auto rootOf = [&](const nl::Game& game) {
        return game.device < store.devices().size() ? store.devices()[game.device] + ":" : std::string("sd:");
    };
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
        // Spec v2 6.2: Slots 200–255 lesen, dann games.ini > config.ini > Automatik.
        const std::vector<nl::CiosEntry> found = nl::platform::scanCios();
        for (const nl::CiosEntry& e : found) {
            nl::log::line("NL_CIOS_SLOT %u %s base=%u ver=%s", static_cast<unsigned>(e.slot), nl::ciosKindName(e.kind),
                          static_cast<unsigned>(e.base), e.version.c_str());
        }
        const std::string id6 = game.key != nullptr ? std::string(game.key).substr(0, 6) : std::string();
        const nl::CiosChoice choice = nl::chooseCios(found, overrides.wiiIos(id6), config.wiiIos);
        const nl::CiosStatus cios = nl::ciosStatusFor(found, choice);
        nl::log::line("NL_WII_IOS %u %s", static_cast<unsigned>(cios.slot), choice.forced ? "forced" : "auto");
        switch (cios.state) {
            case nl::CiosState::D2x: nl::log::line("NL_CIOS ok base=%u ver=%s", static_cast<unsigned>(cios.base), cios.version.c_str()); break;
            case nl::CiosState::Unverified: nl::log::line("NL_CIOS unverified"); break;
            case nl::CiosState::Missing: nl::log::line("NL_CIOS missing"); break;
        }
        u32 partLba = 0;
        const std::string libRoot = rootOf(game);
        const nl::platform::Device* device = nl::platform::deviceTable().byName(libRoot);
        auto frags = device != nullptr
                         ? nl::platform::computeWiiFrags(fs, game, *device, &partLba)
                         : nl::Result<nl::FragList, nl::LaunchError>::err({nl::LaunchFailure::RomMissing, libRoot});
        if (frags.isOk()) {
            const nl::FragList& f = frags.value();
            const auto bytes = nl::serializeFragList(f);
            char first[48] = "-";
            if (!f.entries.empty()) {
                std::snprintf(first, sizeof first, "%u:%u:%u", static_cast<unsigned>(f.entries[0].offset),
                              static_cast<unsigned>(f.entries[0].sector), static_cast<unsigned>(f.entries[0].count));
            }
            nl::log::line("NL_FRAG dev=%s part_lba=%u n=%u size=%u crc=%08x first=%s", device->slot.name.c_str(),
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
        auto target = nl::preflightGame(fs, game, rootOf(game), emulators, &inputs, hwAccess);
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
        if (data.writable && writeText(fs, historyPath.c_str(), store.historyIni().serialize())) nl::log::line("NL_HISTORY_SAVED");
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
            if (data.writable) nl::log::flushLogFile(fs, logPath);
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
            if (data.writable) nl::log::flushLogFile(fs, logPath);
            input.shutdown();
            nl::platform::unmountAll();
            nl::platform::shutdownVideo();
            nl::launch::bootWiiGame(booter.value(), target.value().wii->iosSlot);
        }

        const nl::LaunchPlan& plan = target.value().dol;
        if (auto loaded = nl::launch::loadDolToBuffer(fs, plan); !loaded.isOk()) {
            nl::log::line("NL_WARN DOL nicht geladen: %s", loaded.error().context.c_str());
            showError({nl::LaunchFailure::EmuInvalid, plan.dolPath});
            return;
        }
        stopAudio();
        logPerfSummary();
        if (data.writable) nl::log::flushLogFile(fs, logPath);
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
        // Je Gerät: Cover landen dort, wo das Spiel liegt (oder in dessen Cache unter <data>).
        const std::vector<std::string> roots = coverRootsOf(store.devices());
        std::vector<nl::CoverJob> jobs;
        for (std::size_t d = 0; d < roots.size(); ++d) {
            std::vector<const nl::Game*> games;
            for (const nl::Game& g : store.games())
                if (g.device == d) games.push_back(&g);
            if (games.empty()) continue;
            const auto present = nl::existingCoverFiles(fs, roots[d]);  // vier Ordnerlisten statt zwei je Spiel
            auto part = nl::coverDownloadJobs(games, [&](const std::string& path) { return present.count(path) > 0; },
                                              roots[d]);
            for (nl::CoverJob& j : part) jobs.push_back(std::move(j));
        }
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
        if (!net.start(fs, std::move(jobs), std::move(regions), base)) {
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
            if (data.writable) nl::log::flushLogFile(fs, logPath);
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
        if (data.writable) nl::log::flushLogFile(fs, logPath);
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
                                                 data.writable ? &fs : nullptr, logPath.c_str()};
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
                deviceIndexes = std::move(*result).value().indexes;
                applyIndexes();
                covers.setRoots(coverRootsOf(store.devices()));
                covers.libraryChanged();
                mainScene.setLibrary(libraryView(), focusKey);
                nl::log::line("NL_SCAN_DONE games=%u added=%u removed=%u", static_cast<unsigned>(store.games().size()),
                              static_cast<unsigned>(st.added), static_cast<unsigned>(st.removed));
                saveIndexes();
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
                if (data.writable && writeText(fs, gamesPath.c_str(), overrides.toIni().serialize()))
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
                if (data.writable && writeText(fs, vcMapPath.c_str(), text)) {
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
                if (data.writable && writeText(fs, favoritesPath.c_str(), text)) nl::log::line("NL_FAV_SAVED %s", key.c_str());
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
                if (!worker.start(fs, libDevices, scanRules, {}, metaPtr)) {
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
                if (data.writable) nl::log::flushLogFile(fs, logPath);
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
            if (data.writable) nl::log::flushLogFile(fs, logPath);
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
            if (data.writable) nl::log::flushLogFile(fs, logPath);
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
    if (data.writable) nl::log::flushLogFile(fs, logPath);
    input.shutdown();
    nl::platform::unmountAll();
    return 0;
}
