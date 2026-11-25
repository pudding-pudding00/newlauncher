// Hauptprogramm (M2): Konfiguration → Video → Bibliotheksgerät → Index/Scan → Hauptschleife.

#include <gccore.h>
#include <ogc/lwp_watchdog.h>

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <array>
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
#include "core/self_update.hpp"
#include "core/emu_profiles.hpp"
#include "core/emulators.hpp"
#include "core/index_file.hpp"
#include "core/launch_layout.hpp"
#include "core/launch_plan.hpp"
#include "core/playlist.hpp"
#include "core/library_store.hpp"
#include "core/meta_db.hpp"
#include "core/n64.hpp"
#include "core/options_model.hpp"
#include "core/nin_cfg.hpp"
#include "core/cheat_select.hpp"
#include "core/cheat_source.hpp"
#include "core/cios_scan.hpp"
#include "core/n64_choice.hpp"
#include "core/perf_stats.hpp"
#include "core/theme.hpp"
#include "core/theme_file.hpp"
#include "core/vc_map.hpp"
#include "core/wip_patch.hpp"
#include "gfx/draw_list.hpp"
#include "gfx/font_loader.hpp"
#include "gfx/renderer.hpp"
#include "launch/chainloader.hpp"
#include "launch/return_stub.hpp"
#include "launch/channel.hpp"
#include "launch/wii_boot.hpp"
#include "platform/crash.hpp"
#include "platform/cheat_worker.hpp"
#include "platform/disc_drive.hpp"
#include "platform/emu_worker.hpp"
#include "platform/audio.hpp"
#include "platform/log.hpp"
#include "platform/music.hpp"
#include "platform/http_client.hpp"
#include "platform/net_service.hpp"
#include "platform/net_worker.hpp"
#include "platform/remote_server.hpp"
#include "platform/nand_titles.hpp"
#include "platform/scan_worker.hpp"
#include "platform/std_fs.hpp"
#include "platform/storage.hpp"
#include "platform/system.hpp"
#include "platform/system_probe.hpp"
#include "platform/theme_loader.hpp"
#include "platform/video.hpp"
#include "platform/wii_disc.hpp"

// M15: 1, wenn der Game-Booter den Codehandler aus Quelltext mitbringt (CMake setzt es zusammen mit NL_BOOTER_CHEATS).
#ifndef NL_WII_CHEATS
#define NL_WII_CHEATS 0
#endif
#include "platform/wii_input.hpp"
#include "ui/settings_scene.hpp"
#include "ui/debug_overlay.hpp"
#include "ui/details_scene.hpp"
#include "app/banner_service.hpp"
#include "ui/dialog_scene.hpp"
#include "ui/emulators_scene.hpp"
#include "ui/about_scene.hpp"
#include "ui/cheats_scene.hpp"
#include "ui/options_scene.hpp"
#include "ui/parental_scene.hpp"
#include "ui/pin_scene.hpp"
#include "ui/setup_scene.hpp"
#include "ui/sysinfo_scene.hpp"
#include "core/sysinfo.hpp"
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
constexpr std::size_t kMaxStatsBytes = 2 * 1024 * 1024;  // 5000 Spiele mit langen Pfadschlüsseln ≈ 400 KiB
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

// unreadable (optional): true, wenn die Datei da ist, aber nicht gelesen werden konnte (zu groß, Lesefehler). Wer den
// Text danach komplett neu schreibt, darf das dann nicht tun, sonst ist der alte Inhalt weg.
std::string readText(nl::FileSystem& fs, const char* path, std::size_t maxBytes = kMaxIniBytes,
                     bool* unreadable = nullptr) {
    auto data = nl::readAllWithFallback(fs, path, maxBytes);
    if (unreadable != nullptr) *unreadable = !data.isOk() && data.error().code != nl::ErrorCode::NotFound;
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

// text erhält den gelesenen Inhalt bzw. den Standardtext (Grundlage für configToIniText). firstRun: config.ini fehlt
// in einem beschreibbaren Datenordner; dann schreibt erst der Einrichtungs-Assistent die Datei (M13, Spec v2 6.4).
nl::Config loadConfig(nl::FileSystem& fs, const DataDir& dataDir, std::string& text, bool& firstRun) {
    firstRun = false;
    text = nl::defaultConfigText(kHasTls);
    if (!dataDir.readable) {
        nl::log::line("NL_CONFIG default (kein Datenordner)");
        return nl::configFromIni(nl::IniDoc::parse(text), kHasTls);  // Config{} hätte keine [paths]
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
    firstRun = dataDir.writable;
    nl::log::line(firstRun ? "NL_CONFIG missing" : "NL_CONFIG default");
    return nl::configFromIni(nl::IniDoc::parse(text), kHasTls);  // Standardordner aus dem Standardtext
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
    u64 detailsBackAt = 0;  // [debug] details_back_ms: Zeitpunkt des eingespeisten „Zurück“ (M18-Smoke)
    const u64 bootTicks = gettime();  // Perf: Startzeiten relativ zum Einsprung
    // Spec 9: MEM2 ab kDolBufferAddr gehört dem Chainloader. Vor jeder Allokation reservieren.
    const bool launchAllowed = reinterpret_cast<u32>(SYS_GetArena2Hi()) > nl::kDolBufferAddr;
    if (launchAllowed) SYS_SetArena2Hi(reinterpret_cast<void*>(nl::kDolBufferAddr));
    nl::log::init();
    nl::log::line("newlauncher %s", NL_VERSION);
    nl::platform::logPreviousCrash();
    if (!launchAllowed) nl::log::line("NL_WARN Arena2 schon belegt");

    // 1. SD und Konfiguration (Spec 6.1 Schritt 2)
    nl::platform::StdFileSystem fs;
    const bool sdOk = nl::platform::mountSd();  // Emulatoren und ihre Installation bleiben auf der SD (Spec 6.9)
    const DataDir data = chooseData(fs, argc > 0 && argv != nullptr && argv[0] != nullptr ? argv[0] : "");
    const std::string configPath = data.path("config.ini"), favoritesPath = data.path("favorites.ini"),
                      historyPath = data.path("history.ini"), vcMapPath = data.path("n64_vc.ini"),
                      gamesPath = data.path("games.ini"), logPath = data.path("log.txt");
    std::string configText;
    bool firstRun = false;
    nl::Config config = loadConfig(fs, data, configText, firstRun);  // die Einstellungsszene ändert sie live
    if (data.readable) {  // Smoke-Tests ohne config.ini (M13): [debug] aus <data>/debug.ini, sonst nichts
        auto dbg = nl::readAllWithFallback(fs, data.path("debug.ini"), kMaxIniBytes);
        if (dbg.isOk()) {
            const auto& b = dbg.value();
            nl::applyDebugIni(config, nl::IniDoc::parse(std::string_view(reinterpret_cast<const char*>(b.data()), b.size())));
            nl::log::line("NL_DEBUG_INI");
        }
    }
    // M16-Smoke: Liegt der Datenordner schon auf USB, bleibt die echte USB-Platte eingehängt.
    if (!nl::platform::deviceTable().setSdAsDisk(config.debugSdAsDisk)) nl::log::line("NL_DEV sd_as_disk ignoriert");
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
    const nl::EmulatorTable emulatorIni = loadEmulators(fs, data);
    // Ziel der Emulator-Installation (Spec v2 6.3): SD, ohne SD das <data>-Gerät; leer = keine Installation.
    const std::string emuRoot = sdOk          ? std::string("sd:")
                                : data.writable ? data.dir.substr(0, data.dir.find(':') + 1)
                                                : std::string();
    if (!emuRoot.empty()) {  // Reste einer abgebrochenen Emulator-Installation (Spec 6.9)
        nl::recoverEmuInstall(fs, emuRoot);
        nl::clearEmuTmp(fs, emuRoot);
    }

    // 2. Video, Renderer, Atlas, Theme aus der Konfiguration
    nl::platform::Video video;
    video.init();
    // M20: Fernsteuerung nur auf ausdrücklichen Wunsch (wiiload … --remote oder [debug] remote = 1).
    nl::platform::RemoteServer remote;
    if (nl::hasRemoteFlag(argc, argv) || config.debugRemote) {
        nl::log::line("NL_REMOTE %s", remote.start() ? "start" : "fail");
    }
    // Eigene boot.dol für RESTART: argv[0], wenn es ein Gerätepfad ist (HBC), sonst der Standardort (wiiload).
    const std::string selfDol = [&]() -> std::string {
        const std::string_view a0 = argc > 0 && argv != nullptr && argv[0] != nullptr ? argv[0] : "";
        if (a0.find(":/") != std::string_view::npos && a0.size() > 4 && a0.substr(a0.size() - 4) == ".dol")
            return std::string(a0);
        return "sd:/apps/newlauncher/boot.dol";
    }();
    // M20 (Spec 4.3): Hat uns der Rückkehr-Stub geladen? Fortschrittscode loggen und löschen.
    const bool stubAtBoot = nl::launch::returnStubInstalled();
    if (stubAtBoot) {
        nl::log::line("NL_RETURN_STUB present stage=%u", static_cast<unsigned>(nl::launch::takeReturnStubStage()));
    }
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

    // Theme-Paket (M17, Spec v2 10.2): kaputte Werte nehmen den Standard und stehen als NL_THEME_BAD im Log.
    nl::platform::ThemeBackground themeBackground;
    std::string appliedThemePack;
    auto applyThemePack = [&]() {
        appliedThemePack = config.themePack;
        renderer.setBackground(nullptr, 0, 0);  // alte Texel nicht mehr zeichnen, bevor sie freigegeben werden
        const nl::platform::LoadedTheme loaded = data.readable
            ? nl::platform::loadThemePack(fs, data.dir, config.themePack, themeBackground)
            : nl::platform::LoadedTheme{};
        if (!data.readable) themeBackground.reset();
        theme.setThemes(loaded.pair.light, loaded.pair.dark);
        renderer.setBackground(themeBackground.texels(), themeBackground.width(), themeBackground.height());
        ctx.background = themeBackground.texels() != nullptr;
        ctx.backgroundU1 = themeBackground.u1();
        ctx.backgroundV1 = themeBackground.v1();
        if (config.themePack.empty()) return;
        const nl::Color bar = loaded.pair.light.bar;
        nl::log::line("NL_THEME %s light.bar=#%02X%02X%02X", config.themePack.c_str(), bar.r, bar.g, bar.b);
        for (const std::string& b : loaded.bad) nl::log::line("NL_THEME_BAD %s", b.c_str());
    };
    applyThemePack();

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
    // Emulator-Erkennung (Spec v2 6.3): apps/ auf allen eingehängten Geräten; nach einer Installation erneut.
    std::vector<nl::FoundApp> foundApps;
    nl::EmulatorTable emulators = emulatorIni;
    auto detectEmulators = [&]() {
        foundApps.clear();
        for (const nl::platform::Device& d : devTable.devices()) {
            if (!d.mounted) continue;
            std::vector<nl::FoundApp> apps = nl::scanApps(fs, d.slot.name);
            foundApps.insert(foundApps.end(), apps.begin(), apps.end());
        }
        emulators = nl::resolveTable(emulatorIni, foundApps);
        for (const nl::ResolvedEmuSummary& e : nl::emuSummaries(emulators))
            nl::log::line("NL_EMU %s %s %s %s", e.section.c_str(), e.profile.empty() ? "-" : e.profile.c_str(),
                          e.how.c_str(), e.path.c_str());
    };
    detectEmulators();
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
    // Eigene Kategorien (M17, Spec 10.3): Rohtext bleibt mit Kommentaren erhalten, nur geänderte Zeilen kommen dazu.
    const std::string categoriesPath = data.path("categories.ini");
    bool categoriesUnreadable = false;
    nl::Categories categories = nl::Categories::fromText(
        data.readable ? readText(fs, categoriesPath.c_str(), kMaxIniBytes, &categoriesUnreadable) : std::string());
    if (categoriesUnreadable) nl::log::line("NL_WARN categories.ini nicht lesbar, wird nicht überschrieben");
    nl::log::line("NL_CATEGORIES n=%u", static_cast<unsigned>(categories.names().size()));
    // M17: Spielstatistik, unabhängig von history.ini
    const std::string statsPath = data.path("stats.ini");
    bool statsUnreadable = false;
    nl::PlayStats playStats = nl::PlayStats::fromText(
        data.readable ? readText(fs, statsPath.c_str(), kMaxStatsBytes, &statsUnreadable) : std::string());
    if (statsUnreadable) nl::log::line("NL_WARN stats.ini nicht lesbar, wird nicht überschrieben");
    nl::log::line("NL_STATS n=%u", static_cast<unsigned>(playStats.size()));

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
        for (const nl::Game& g : store.games()) {
            const char* dev = (g.flags & nl::kFlagDisc) != 0 ? "disc"
                            : g.device < store.devices().size() ? store.devices()[g.device].c_str()
                                                                : "-";
            nl::log::line("NL_GAME %s %s path=%s", g.key, dev, g.path != nullptr ? g.path : "");
        }
    };
    // Review Focus 3: kaputte WBFS-Slots fallen still aus der Bibliothek, das Log nennt sie (Haupt-Thread, log ist
    // nicht threadfest).
    auto logWbfsSkips = [](const nl::MultiScan& m) {
        for (const auto& [dev, slot] : m.wbfsSkips)
            nl::log::line("NL_WBFS_SKIP %u dev=%s", static_cast<unsigned>(slot), dev.c_str());
    };
    auto applyIndexes = [&]() {
        store.setGames(nl::mergeIndexes(deviceIndexes), metaPtr);
        logGames();
    };
    nl::EmuNandInfo emuNandInfo;
    u32 emuNandCount = 0;
    auto scanEmuNandChannels = [&]() {
        std::vector<nl::DeviceSlot> mountedSlots;
        for (const nl::platform::Device& d : devTable.devices())
            if (d.mounted) mountedSlots.push_back(d.slot);
        emuNandInfo = nl::checkEmuNand(fs, config.emuNandPath, mountedSlots);
        if (emuNandInfo.status == nl::EmuNandStatus::Ok) {
            u32 skipped = 0;
            auto titles = nl::scanEmuNand(fs, emuNandInfo.path, nl::platform::bannerLangFor(nl::ui::language()), &skipped);
            emuNandCount = static_cast<u32>(titles.size());
            nl::log::line("NL_EMUNAND titles=%u skipped=%u", static_cast<unsigned>(emuNandCount),
                          static_cast<unsigned>(skipped));
            if (skipped > 0) nl::log::line("NL_EMUNAND_SKIP %u", static_cast<unsigned>(skipped));
            store.setChannels(std::move(titles));
        } else {
            emuNandCount = 0;
            store.setChannels({});
        }
    };

    nl::platform::ScanWorker worker;
    nl::ScanRules scanRules = nl::defaultScanRules();
    scanRules.zipConsoles = nl::zipConsoles(emulators);  // RetroArch-Kerne bekommen keine ZIPs (Spike M13)
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
            logWbfsSkips(result->value());
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
    scanEmuNandChannels();
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
    if (data.readable) settings.setThemePacks(nl::listThemePacks(fs, data.dir));
    settings.setActiveSources(libFellBack ? nl::SourceSpec{false, {"sd"}} : sources);
    // Wurzel des Geräts, auf dem ein Spiel liegt ("usb2:"); Game::device zählt in store.devices().
    auto rootOf = [&](const nl::Game& game) {
        if (game.device == nl::kNoDevice) return std::string();
        return game.device < store.devices().size() ? store.devices()[game.device] + ":" : std::string("sd:");
    };
    homeMenu.setSettings(&settings);
    nl::platform::setTlsCaPath(config.debugTlsCa);  // vor dem ersten submit; geladen beim ersten HTTPS-Auftrag
    nl::platform::setNetPerf(config.debugPerf);
    nl::platform::NetService netService;  // ein Netz-Thread für Cover und Emulatoren (M11); lebt länger als die Worker
    nl::platform::NetWorker net(netService);
    mainScene.setFavoriteKeys(&store);
    mainScene.setCategories(&categories);
    mainScene.setSort(config.sortOrder, &playStats);
    nl::ui::JumpBar jumpBar(mainScene);
    mainScene.setJumpBar(&jumpBar);
    nl::ui::Keyboard keyboard(mainScene);
    keyboard.setFilter(&mainScene);  // Filterzeile im Such-Overlay (Spec v2 10.3)
    mainScene.setKeyboard(&keyboard);
    nl::ui::DetailsScene details;
    mainScene.setDetails(&details, &store);
    details.setCovers(&covers);
    nl::app::BannerService banners(fs, renderer);
    auto bannerRootsOf = [](std::span<const std::string> devices) {
        std::vector<std::string> roots;
        for (const std::string& dev : devices) roots.push_back(dev + ":");
        return roots;
    };
    banners.setRoots(bannerRootsOf(store.devices()));
    banners.setGerman(nl::ui::language() == nl::ui::Lang::De);
    banners.setDebugFile(config.debugBannerFile);
    banners.setAudio(&audio, &music);
    banners.setVcMap(&vcMapping);
    banners.setLogSounds(config.debugLogSounds);
    banners.setEnabled(config.banners);
    details.setBanners(&banners);
    nl::platform::DiscDrive discDrive;
    if (!config.debugFakeDisc.empty()) {
        if (auto fake = nl::parseFakeDisc(config.debugFakeDisc)) discDrive.setFakeDisc(*fake);
        else nl::log::line("NL_CFG_BAD debug.fake_disc");
    }
    bool hasDiscInStore = false;
    auto applyDiscResult = [&](const nl::platform::DiscReadResult& r, bool userInitiated) {
        if (r.status == nl::platform::DiscReadStatus::Ok) {
            const std::string focusKey = mainScene.focusedKey();
            store.setDisc(r.disc, metaPtr);
            hasDiscInStore = true;
            mainScene.setLibrary(libraryView(), focusKey);
            nl::log::line("NL_DISC %s %s", r.disc.kind == nl::DiscKind::Gc ? "gc" : "wii", r.disc.id6.c_str());
            logGames();
        } else {
            if (hasDiscInStore) {
                const std::string focusKey = mainScene.focusedKey();
                store.setDisc(std::nullopt);
                hasDiscInStore = false;
                mainScene.setLibrary(libraryView(), focusKey);
            }
            if (r.status == nl::platform::DiscReadStatus::Unreadable) {
                nl::log::line("NL_DISC unreadable");
                if (userInitiated) mainScene.showToast(nl::ui::tr(nl::ui::Str::DiscUnreadable));
            } else if (userInitiated) {
                mainScene.showToast(nl::ui::tr(nl::ui::Str::NoDisc));
            }
        }
    };
    nl::ui::DialogScene dialog;
    // Kindersicherung (M17, Spec v2 10.5): wirksam ist config.parental oder, mit use_wii, der Wii-Jugendschutz.
    // Die PIN steht nie im Log; SYSCONF wird nur gelesen.
    nl::ui::PinScene pinScene;
    nl::ui::ParentalScene parentalScene;
    parentalScene.setConfig(&config.parental);
    nl::PinGate pinGate;
    // Eine Sperre aus dem letzten Lauf gilt weiter (Neustart umgeht die 30 s nicht). Höchstens 30 s, falls die
    // Uhr seitdem zurückgestellt wurde.
    if (const std::time_t t = std::time(nullptr); t > 0 && config.parental.lockUntil != 0) {
        const u32 now = static_cast<u32>(t);
        const u32 rest = config.parental.lockUntil > now ? std::min<u32>(config.parental.lockUntil - now, 30) : 0;
        if (rest > 0) {
            pinGate.blockFor(rest * 1000u, msSince(bootTicks));
            nl::log::line("NL_PARENTAL locked %u", static_cast<unsigned>(rest));
        }
        // Abgelaufen oder zu weit in der Zukunft (Uhr zurückgestellt): auf die gekappte Restzeit kürzen, damit
        // nicht jeder weitere Start erneut 30 s sperrt.
        const u32 kept = rest > 0 ? now + rest : 0;
        if (kept != config.parental.lockUntil) {
            config.parental.lockUntil = kept;
            saveConfig();
        }
    }
    nl::ParentalConfig parental;
    std::optional<nl::WiiParental> wiiParental;
    bool wiiParentalRead = false;
    std::array<u8, 16> wiiSalt{};
    nl::platform::randomBytes(wiiSalt);
    auto refreshParental = [&]() {
        if (config.parental.useWii && !wiiParentalRead) {
            wiiParental = nl::platform::readWiiParental();
            wiiParentalRead = true;
            if (wiiParental)
                nl::log::line("NL_PARENTAL_WII %s system=%s age=%u", wiiParental->enabled ? "on" : "off",
                              nl::ratingSystemName(wiiParental->system), static_cast<unsigned>(wiiParental->maxAge));
            else nl::log::line("NL_PARENTAL_WII none");
        }
        parental = config.parental.useWii && wiiParental ? nl::applyWiiParental(config.parental, *wiiParental, wiiSalt)
                                                          : config.parental;
    };
    refreshParental();
    nl::log::line("NL_PARENTAL %s", parental.enabled ? "on" : "off");
    mainScene.setParental(&parental, &overrides);
    nl::ui::VcMapScene vcMapScene;
    nl::platform::EmuWorker emu(netService);
    nl::platform::CheatWorker cheatWorker(netService);  // M15: Gecko-Codes laden
    nl::ui::EmulatorsScene emuScene;
    nl::ui::AboutScene aboutScene;
    nl::ui::OptionsScene optionsScene;
    nl::ui::CheatsScene cheatsScene;
    nl::Console cheatsConsole = nl::Console::Wii;  // Konsole des Spiels in der Cheat-Szene
#if defined(NL_TLS) && NL_TLS  // ab M11 Task 4 vom CMake-Schalter gesetzt
    aboutScene.setBuildInfo(NL_VERSION, true);
#else
    aboutScene.setBuildInfo(NL_VERSION, false);
#endif
    std::vector<nl::OscApp> emuList;  // zuletzt geladene OSC-Liste; leer bis „Nach Updates suchen“
    const std::string emuRepo = config.debugEmuRepo.empty() ? config.emulatorRepo : config.debugEmuRepo;
    nl::ui::SceneStack scenes;
    scenes.push(&mainScene);
    // Systeminfo (M13, Spec v2 6.1): beim Öffnen frisch gesammelt; MAC und IP kommen nie hinein.
    nl::ui::SysInfoScene sysInfoScene;
    auto buildSysInfo = [&]() {
        nl::SystemInfo s;
        s.sysMenuVersion = nl::platform::sysMenuVersion();
        s.platform = nl::platformForSysMenu(s.sysMenuVersion, nl::platform::latteDetected());
        s.region = nl::regionFromSysMenu(s.sysMenuVersion);
        s.launcherVersion = NL_VERSION;
        s.launcherIos = nl::platform::runningIos();
        s.ahbprot = ahbprotOff;
        s.cios = nl::platform::scanCios();
        s.ciosSlot = nl::chooseCios(s.cios, std::nullopt, config.wiiIos).slot.value_or(0);
        s.gcPorts = s.platform != nl::Platform::VWii;
        for (const nl::platform::Device& d : devTable.devices()) {
            if (!d.mounted && d.slot.skip == nl::SkipReason::None) continue;
            nl::DeviceSummary sum;
            sum.name = d.slot.name;
            sum.fs = nl::fsKindName(d.slot.kind);
            sum.total = d.totalBytes;
            sum.free = d.freeBytes;
            sum.readOnly = d.mounted && !d.writable;
            if (!d.mounted) {  // Spec v2 9.3: nicht eingehängte Partitionen mit Grund (M16)
                sum.mounted = false;
                sum.reasonDe = nl::skipReasonText(d.slot.skip, true);
                sum.reasonEn = nl::skipReasonText(d.slot.skip, false);
                s.devices.push_back(std::move(sum));
                continue;
            }
            for (const nl::Game& g : store.games())
                if (g.device < store.devices().size() && store.devices()[g.device] == d.slot.name) ++sum.games;
            s.devices.push_back(std::move(sum));
        }
        s.emulators = nl::emuSummaries(emulators);
        s.netUp = netService.networkUp();  // Stand nach dem letzten Netzaufbau (volatile, nur gelesen)
        s.timeSynced = nl::platform::clockSynced();
        s.tls = kHasTls && netService.tlsUp();
        s.emuNandDe = nl::emuNandStatusText(emuNandInfo, emuNandCount, true);
        s.emuNandEn = nl::emuNandStatusText(emuNandInfo, emuNandCount, false);
        return s;
    };
    auto openSysInfo = [&]() {
        sysInfoScene.setInfo(buildSysInfo());
        scenes.push(&sysInfoScene);
        nl::log::line("NL_SCENE sysinfo");
    };
    auto saveSysInfo = [&]() {
        const std::string path = data.path("sysinfo.txt");
        const bool ok = data.writable &&
                        writeText(fs, path.c_str(),
                                  nl::formatSysInfo(sysInfoScene.info(), nl::ui::language() == nl::ui::Lang::De));
        sysInfoScene.setSaveResult(path, ok);
        if (ok) nl::log::line("NL_SYSINFO_SAVED %s", path.c_str());
        else nl::log::line("NL_SYSINFO_SAVE fail");
    };

    // Einrichtungs-Assistent (M13, Spec v2 6.4): beim ersten Start ohne config.ini und über Einstellungen.
    nl::ui::SetupScene setupScene;
    volatile int setupNet = 0;  // Netztest: 0 läuft/keiner, 1 ok, 2 ok mit Zeitabgleich, 3 fehlgeschlagen (Netz-Thread schreibt)
    auto openSetup = [&]() {
        std::vector<nl::DeviceGames> games;
        for (const nl::platform::Device& d : devTable.devices()) {
            if (!d.mounted) continue;
            nl::DeviceGames g;
            g.device = d.slot.name;
            for (const nl::Game& game : store.games())
                if (game.device < store.devices().size() && store.devices()[game.device] == d.slot.name)
                    ++g.games[static_cast<std::size_t>(game.console)];
            games.push_back(std::move(g));
        }
        setupScene.start(nl::SetupWizard(buildSysInfo(), config, games));
        std::string counts;  // Log: Spiele je Gerät, Grundlage des Quellen-Vorschlags
        for (const nl::DeviceGames& g : games) counts += " " + g.device + "=" + std::to_string(g.total());
        nl::log::line("NL_WIZARD start%s", counts.c_str());
    };
    // Verbindungstest gegen die Cover-Quelle; jede HTTP-Antwort (auch 404) zählt als erreichbar. Mit TLS vorher
    // einmal SNTP. Läuft wie alle wtls-Aufrufe im Netz-Thread.
    auto startSetupNetTest = [&]() {
        setupNet = 0;
        const std::string base = config.debugCoverUrl.empty() ? config.coverBase : config.debugCoverUrl;
        const u32 id = netService.submit([&setupNet, &netService, base](const volatile bool& cancel) {
            if (!netService.networkUp()) {
                setupNet = 3;
                return;
            }
            const bool timeSet = kHasTls && nl::platform::syncClockOnce();
            bool ok = false;
            if (const auto url = nl::parseUrl(base)) {
                std::vector<u8> body;
                ok = nl::platform::httpGet(*url, 64 * 1024, body, &cancel).status != 0;
            }
            setupNet = ok ? (timeSet ? 2 : 1) : 3;
        });
        if (id == 0) setupNet = 3;
    };
    // Schreibt eine Datei mit Sicherung: vorhandene Datei → <datei>.bak, dann neu über .tmp und Umbenennen.
    auto writeWithBackup = [&](const std::string& path, const std::string& text) {
        auto old = nl::readAllWithFallback(fs, path, kMaxIniBytes);
        if (old.isOk()) {
            const auto& b = old.value();
            if (!fs.writeAtomic(nl::backupName(path), std::span<const u8>(b.data(), b.size())).isOk()) return false;
        }
        return writeText(fs, path.c_str(), text);
    };
    // Ergebnis übernehmen: config.ini (und bei „festschreiben“ emulators.ini) mit Sicherung schreiben, Sprache
    // sofort anwenden. Quellen und Emulatoren gelten ab dem nächsten Start (wie in den Einstellungen).
    auto finishSetup = [&]() {
        if (!setupScene.active()) return;
        const nl::SetupWizard& w = setupScene.wizard();
        nl::Config next = w.result();
        const std::string text = nl::configToIniText(next, configText);
        bool ok = data.writable && writeWithBackup(configPath, text);
        if (ok) configText = text;
        if (ok) {
            if (const auto emu = w.emulatorsIni()) ok = writeWithBackup(data.path("emulators.ini"), *emu);
        }
        config = std::move(next);
        firstRun = false;
        const nl::ui::Lang lang = nl::ui::resolveLanguage(config.language, systemGerman);
        if (lang != nl::ui::language()) {
            nl::ui::setLanguage(lang);
            banners.setGerman(lang == nl::ui::Lang::De);  // GC-Banner: Titel beim nächsten Laden in der neuen Sprache
            channelsLoaded = false;
            scanEmuNandChannels();
            mainScene.setLibrary(libraryView(), mainScene.focusedKey());
            nl::log::line("NL_LANG %s", nl::ui::langKey(lang));
        }
        mainScene.showToast(nl::ui::tr(ok ? nl::ui::Str::SetupSaved : nl::ui::Str::SetupSaveFailed));
        nl::log::line(ok ? "NL_WIZARD done" : "NL_WIZARD save_fail");
    };

    // Zeile „Startmethode“ der Detailansicht aus Override, Zuordnung und Verfügbarkeit.
    auto refreshDetailsN64 = [&]() {
        const nl::Game* g = details.game();
        if (g == nullptr || g->console != nl::Console::N64) return;
        details.setN64(nl::decideN64(n64Inputs(*g)), overrides.n64Method(g->key));
    };

    // M15: <ID6>.txt und <ID6>.sel lesen (fehlend oder zu groß → leer); true, wenn die .txt vorliegt.
    auto readCheatFiles = [&](const std::string& id6, std::string& txt, std::string& sel) {
        txt.clear();
        sel.clear();
        if (!data.readable) return false;
        const std::string txtPath = nl::cheatTxtPath(data.dir, id6);
        std::array<u8, 1> probe{};
        const bool exists = fs.read(txtPath, 0, probe).isOk();
        if (auto r = fs.readAll(txtPath, nl::kMaxCheatTxtBytes); r.isOk())
            txt.assign(reinterpret_cast<const char*>(r.value().data()), r.value().size());
        if (auto r = fs.readAll(nl::cheatSelPath(data.dir, id6), kMaxIniBytes); r.isOk())
            sel.assign(reinterpret_cast<const char*>(r.value().data()), r.value().size());
        return exists;
    };

    // Spec 9: Preflight, Verlauf, Chainload. Kehrt nur zurück, wenn der Start scheitert (dann mit Dialog).
    // Wii-Spiele (M9 Task 5): cIOS prüfen, Fragmentliste rechnen und gegenprüfen, dann preflightWii.
    auto prepareWii = [&](const nl::Game& game, const nl::IniDoc& configDoc, const nl::IniDoc& gamesDoc) {
        // Spec v2 6.2: Slots 200–255 lesen, dann games.ini > config.ini > Automatik.
        const std::vector<nl::CiosEntry> found = nl::platform::scanCios();
        for (const nl::CiosEntry& e : found) {
            nl::log::line("NL_CIOS_SLOT %u %s base=%u ver=%s", static_cast<unsigned>(e.slot), nl::ciosKindName(e.kind),
                          static_cast<unsigned>(e.base), e.version.c_str());
        }
        const std::string id6 = game.key != nullptr ? std::string(game.key).substr(0, 6) : std::string();
        // Spec v2 7: Spieloptionen; ios aus [game]/[wii] geht als Spielwahl an chooseCios.
        nl::WiiOptions options = nl::resolveWiiOptions(configDoc, gamesDoc, id6);
        for (const std::string& k : options.bad) nl::log::line("NL_OPT_BAD %s", k.c_str());
        if (options.returnToHbc) options.cfg.returnTo = nl::hbcReturnTo(installedTitles);
        const nl::CiosChoice choice = nl::chooseCios(found, options.ios ? options.ios : overrides.wiiIos(id6), config.wiiIos);
        const nl::CiosStatus cios = nl::ciosStatusFor(found, choice);
        nl::log::line("NL_WII_IOS %u %s", static_cast<unsigned>(cios.slot), choice.forced ? "forced" : "auto");
        switch (cios.state) {
            case nl::CiosState::D2x: nl::log::line("NL_CIOS ok base=%u ver=%s", static_cast<unsigned>(cios.base), cios.version.c_str()); break;
            case nl::CiosState::Unverified: nl::log::line("NL_CIOS unverified"); break;
            case nl::CiosState::Missing: nl::log::line("NL_CIOS missing"); break;
        }
        const bool isDisc = (game.flags & nl::kFlagDisc) != 0;
        u32 partLba = 0;
        const std::string libRoot = rootOf(game);
        nl::Result<nl::FragList, nl::LaunchError> frags =
            nl::Result<nl::FragList, nl::LaunchError>::err({nl::LaunchFailure::RomMissing, libRoot});
        if (!isDisc) {
            const nl::platform::Device* device = nl::platform::deviceTable().byName(libRoot);
            if (device != nullptr) frags = nl::platform::computeWiiFrags(fs, game, *device, &partLba);
            if (frags.isOk()) {
                const nl::FragList& f = frags.value();
                const auto bytes = nl::serializeFragList(f);
                char first[48] = "-";
                if (!f.entries.empty()) {
                    std::snprintf(first, sizeof first, "%u:%u:%u", static_cast<unsigned>(f.entries[0].offset),
                                  static_cast<unsigned>(f.entries[0].sector), static_cast<unsigned>(f.entries[0].count));
                }
                nl::log::line("NL_FRAG dev=%s part_lba=%u n=%u size=%u crc=%08x first=%s", device->slot.name.c_str(),
                              static_cast<unsigned>(partLba), static_cast<unsigned>(f.entries.size()),
                              static_cast<unsigned>(f.size), static_cast<unsigned>(nl::fragListCrc(bytes)), first);
            }
        }
        // M15: Cheats nur mit Codehandler im Build und Option „Cheats“ (Spec v2 8); sonst leere GCT, hooktype 0.
        std::vector<u8> gct;
        if (NL_WII_CHEATS != 0 && options.cheats) {
            std::string txt, sel;
            readCheatFiles(id6, txt, sel);
            gct = nl::gctForSelection(txt, sel);
        }
        // M15: WIP-Patches aus <data>/codes/<ID6>.wip (wie WiiFlow ohne eigene Option); kaputte Zeilen nur ins Log.
        nl::WipFile wip;
        if (data.readable) {
            if (auto r = fs.readAll(nl::wipPath(data.dir, id6), nl::kMaxWipBytes); r.isOk()) {
                wip = nl::parseWip(std::string_view(reinterpret_cast<const char*>(r.value().data()), r.value().size()));
                for (u32 line : wip.badLines) nl::log::line("NL_WIP_SKIP line=%u", static_cast<unsigned>(line));
                nl::log::line("NL_WII_WIP n=%u skipped=%u", static_cast<unsigned>(wip.entries.size()),
                              static_cast<unsigned>(wip.skipped));
            }
        }
        auto plan = [&]() -> nl::Result<nl::WiiBootPlan, nl::LaunchError> {
            if (!isDisc) {
                return nl::preflightWii(game, libRoot, cios, frags, config.debugWiiDryRun, options, gct, wip.entries,
                                        &emuNandInfo);
            }
            const nl::platform::DiscReadResult cur = discDrive.readSync();
            std::optional<nl::OpticalDisc> curDisc;
            if (cur.status == nl::platform::DiscReadStatus::Ok) curDisc = cur.disc;
            auto p = nl::preflightWiiDisc(game, curDisc, cios, config.debugWiiDryRun, options, gct, wip.entries,
                                          &emuNandInfo);
            if (!p.isOk() && (p.error().kind == nl::LaunchFailure::DiscChanged ||
                              p.error().kind == nl::LaunchFailure::DiscUnreadable)) {
                applyDiscResult(cur, false);
            }
            return p;
        }();
        if (plan.isOk() && isDisc) {
            nl::log::line("NL_WII_DISC id=%s ios=%u", plan.value().id6.c_str(),
                          static_cast<unsigned>(plan.value().iosSlot));
        }
        if (plan.isOk() && !plan.value().gct.empty()) {
            nl::log::line("NL_WII_CHEATS bytes=%u hook=%u", static_cast<unsigned>(plan.value().gct.size()),
                          static_cast<unsigned>(plan.value().hookType));
        }
        if (plan.isOk()) {
            const nl::WiiCfgFields& c = plan.value().options.cfg;
            nl::log::line("NL_WII_CFG vid=%u vpatch=%u lang=%02x aspect=%d defl=%u width=%u online=%u ios=%u ret=%08x",
                          static_cast<unsigned>(c.vidMode), static_cast<unsigned>(c.patchVidMode),
                          static_cast<unsigned>(c.language), static_cast<int>(c.aspectRatio), static_cast<unsigned>(c.deflicker),
                          static_cast<unsigned>(c.videoWidth), static_cast<unsigned>(c.privateServer),
                          static_cast<unsigned>(plan.value().iosSlot), static_cast<unsigned>(c.returnTo));
        }
        return plan;
    };

    std::string warnedLaunch;  // Spec 7.3: Spiel, dessen Preflight-Hinweis schon gezeigt wurde; nächstes A startet
    // M17: vor jedem Start (auch im Wii-Dry-Run) zählen und sofort schreiben: nach dem Start kommt der Launcher
    // nicht mehr zurück.
    auto recordStats = [&](const std::string& key) {
        playStats.record(key, static_cast<u32>(std::time(nullptr)));
        const nl::PlayStat st = playStats.get(key);
        if (data.writable && !statsUnreadable && writeText(fs, statsPath.c_str(), playStats.toText()))
            nl::log::line("NL_STATS_SAVED %s starts=%u", key.c_str(), static_cast<unsigned>(st.starts));
        else
            nl::log::line("NL_WARN stats.ini nicht gespeichert");
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
        // games.ini [emulator] <schlüssel> = <profil> gilt nur für dieses Spiel (Spec v2 6.3).
        nl::EmulatorTable gameEmus = emulators;
        if (const auto profile = overrides.emulatorProfile(game.key)) {
            if (const auto r = nl::resolveEmulator(game.console, emulatorIni, foundApps, *profile)) {
                if (r->badProfile) nl::log::line("NL_OPT_BAD emulator %s", game.key);
                gameEmus.set(nl::consoleInfo(game.console).key, nl::EmulatorEntry{r->dol, r->args, r->fromIni, r->profile, r->fsMask});
            }
        }
        // Spieloptionen (Spec v2 7) bei jedem Start frisch lesen: die Options-Szene ändert games.ini/config.ini.
        const nl::IniDoc configDoc = data.readable ? readIni(fs, configPath.c_str()) : nl::IniDoc{};
        const nl::IniDoc gamesDoc = data.readable ? readIni(fs, gamesPath.c_str()) : nl::IniDoc{};
        // M15: GC-Cheats gehen über Nintendont; die GCT entsteht hier, wirksam nur mit Option „cheats“.
        std::vector<u8> gcGct;
        if (game.console == nl::Console::Gc && game.key != nullptr) {
            std::string txt, sel;
            readCheatFiles(std::string(game.key).substr(0, 6), txt, sel);
            gcGct = nl::gctForSelection(txt, sel);
        }
        const nl::Platform currentPlatform =
            nl::platformForSysMenu(nl::platform::sysMenuVersion(), nl::platform::latteDetected());
        const nl::GameOptionInputs optionInputs{configDoc, gamesDoc, currentPlatform != nl::Platform::VWii, gcGct,
                                                currentPlatform};
        if (game.console == nl::Console::Gc && (game.flags & nl::kFlagDisc) != 0) {
            const nl::platform::DiscReadResult cur = discDrive.readSync();
            if (cur.status != nl::platform::DiscReadStatus::Ok) {
                applyDiscResult(cur, false);
                nl::log::line("NL_PREFLIGHT_FAIL %s %s", nl::launchFailureName(nl::LaunchFailure::DiscUnreadable), key.c_str());
                showError({nl::LaunchFailure::DiscUnreadable, key});
                return;
            }
            if (cur.disc.kind != nl::DiscKind::Gc || cur.disc.id6 != key.substr(0, 6)) {
                applyDiscResult(cur, false);
                nl::log::line("NL_PREFLIGHT_FAIL %s %s", nl::launchFailureName(nl::LaunchFailure::DiscChanged), key.c_str());
                showError({nl::LaunchFailure::DiscChanged, cur.disc.id6});
                return;
            }
        }
        // M16: Dateisystem des Spielgeräts (Nintendont nur FAT, Emulatoren laut fsMask).
        const nl::platform::Device* gameDev = nl::platform::deviceTable().byName(rootOf(game));
        const nl::FsKind gameFs = gameDev != nullptr ? gameDev->slot.kind : nl::FsKind::Fat;
        auto target = nl::preflightGame(fs, game, rootOf(game), gameEmus, &inputs, hwAccess, &optionInputs, gameFs);
        if (target.isOk() && target.value().kind == nl::LaunchTarget::Kind::EmuChannel) {
            const std::vector<nl::CiosEntry> found = nl::platform::scanCios();
            const nl::WiiOptions options = nl::resolveWiiOptions(configDoc, gamesDoc, "");
            const nl::CiosChoice choice = nl::chooseCios(found, options.ios, config.wiiIos);
            const nl::CiosStatus cios = nl::ciosStatusFor(found, choice);
            if (auto plan = nl::preflightEmuChannel(game, cios, emuNandInfo, config.debugWiiDryRun, options); plan.isOk()) {
                nl::log::line("NL_CHANNEL %s emu=1", key.c_str());
                if (config.debugWiiDryRun) {
                    recordStats(key);
                    nl::log::line("NL_WII_DRYRUN %s", key.c_str());
                    return;
                }
                target.value().kind = nl::LaunchTarget::Kind::Wii;
                target.value().wii = std::move(plan.value());
            } else {
                target = nl::Result<nl::LaunchTarget, nl::LaunchError>::err(plan.error());
            }
        }
        if (target.isOk() && target.value().kind == nl::LaunchTarget::Kind::Wii) {
            if (auto plan = prepareWii(game, configDoc, gamesDoc); plan.isOk()) {
                if (config.debugWiiDryRun) {
                    recordStats(key);
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
        if (const auto& gc = target.value().gc) {
            for (const std::string& k : gc->bad) nl::log::line("NL_OPT_BAD %s", k.c_str());
            for (const std::string& k : gc->unsupported) nl::log::line("NL_OPT_UNSUPPORTED %s", k.c_str());
            const nl::NinConfigInput& n = gc->nin;
            const std::string& cfg = target.value().dol.args[1];
            const unsigned flags = (static_cast<unsigned>(static_cast<unsigned char>(cfg[0x08])) << 24) |
                                   (static_cast<unsigned>(static_cast<unsigned char>(cfg[0x09])) << 16) |
                                   (static_cast<unsigned>(static_cast<unsigned char>(cfg[0x0A])) << 8) |
                                   static_cast<unsigned>(static_cast<unsigned char>(cfg[0x0B]));
            nl::log::line("NL_NIN_CFG flags=%08x vid=%08x lang=%u blocks=%u pads=%u", flags, static_cast<unsigned>(n.videoMode),
                          static_cast<unsigned>(n.language), static_cast<unsigned>(n.memcardBlocks), static_cast<unsigned>(n.maxPads));
        }
        const std::vector<nl::LaunchWarning>& warnings = target.value().warnings;
        for (nl::LaunchWarning w : warnings) nl::log::line("NL_PREFLIGHT_WARN %s", nl::launchWarningName(w));
        if (!warnings.empty() && warnedLaunch != key) {  // erst zeigen, beim nächsten Versuch trotzdem starten
            warnedLaunch = key;
            char title[64];
            char text[320];
            nl::ui::launchWarningText(warnings, title, sizeof title, text, sizeof text, target.value().emuName);
            dialog.open(title, text);
            scenes.push(&dialog);
            audio.play(nl::UiSound::Error);
            nl::log::line("NL_PREFLIGHT_HOLD %s", key.c_str());
            return;
        }
        warnedLaunch.clear();
        if (const std::string& gctPath = target.value().gctPath; !gctPath.empty()) {
            // Nintendont liest CheatPath auf dem Spielgerät. Schlägt das Schreiben fehl, darf keine alte Auswahl
            // liegen bleiben: Datei entfernen, Start ohne Codes (Nintendont meldet die fehlende Datei nur im Log).
            const std::string dir = gctPath.substr(0, gctPath.rfind('/'));
            if (fs.makeDirs(dir).isOk() && fs.writeAtomic(gctPath, target.value().gct).isOk()) {
                nl::log::line("NL_NIN_CHEAT %s bytes=%u", gctPath.c_str(), static_cast<unsigned>(target.value().gct.size()));
            } else {
                static_cast<void>(fs.removeFile(gctPath));
                nl::log::line("NL_WARN GCT nicht geschrieben: %s", gctPath.c_str());
            }
        }
        recordStats(key);
        store.recordPlayed(key, static_cast<u32>(std::time(nullptr)));
        if (data.writable && writeText(fs, historyPath.c_str(), store.historyIni().serialize())) nl::log::line("NL_HISTORY_SAVED");
        else nl::log::line("NL_WARN history.ini nicht gespeichert");
        nl::log::line("NL_LAUNCH %s", key.c_str());
        worker.cancelAndJoin();  // der Scan liest sonst parallel von der Karte
        discDrive.shutdown();
        net.join();
        emu.join();
        cheatWorker.join();
        const bool keepRemote = remote.running();
        remote.stop();  // vor dem Netz: der Fernsteuerungs-Thread nutzt dessen Sockets
        for (const std::string& line : remote.takeLogLines()) nl::log::line("%s", line.c_str());
        netService.shutdown();  // wtls_shutdown im Netz-Thread, dann Thread beendet
        covers.shutdown();
        banners.shutdown();

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
            nl::launch::bootWiiGame(booter.value(), *target.value().wii);
        }

        const nl::LaunchPlan& plan = target.value().dol;
        if (auto loaded = nl::launch::loadDolToBuffer(fs, plan); !loaded.isOk()) {
            nl::log::line("NL_WARN DOL nicht geladen: %s", loaded.error().context.c_str());
            showError({nl::LaunchFailure::EmuInvalid, plan.dolPath});
            return;
        }
        std::vector<std::string> stubArgs;
        if (keepRemote) stubArgs.push_back("--remote");
        nl::launch::installReturnStub(selfDol, stubArgs);  // M20: exit() der DOL führt zurück zu newlauncher
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
    // Gecko-Codes eines Spiels laden (M15): nur auf Knopfdruck, Ziel <data>/codes/<ID6>.txt.
    const std::string cheatSourceTmpl = config.debugCheatUrl.empty() ? config.cheatSource : config.debugCheatUrl;
    std::string cheatFetchId6;  // Spiel des laufenden bzw. letzten Auftrags
    auto startCheatFetch = [&](const std::string& id6, bool overwrite) {
        if (cheatSourceTmpl.empty() || !data.writable || cheatWorker.running()) return false;
        cheatFetchId6 = id6;
        if (!cheatWorker.start(fs, nl::cheatUrl(cheatSourceTmpl, id6), id6, nl::cheatTxtPath(data.dir, id6), overwrite)) {
            nl::log::line("NL_WARN Cheat-Auftrag nicht angenommen");
            return false;
        }
        return true;
    };
    // Codeliste und Auswahl eines Spiels lesen; exists: <ID6>.txt liegt vor (auch wenn zu groß oder kaputt).
    auto loadCheatModel = [&](nl::Console c, const std::string& id6, bool& exists) {
        std::string txt, sel;
        exists = readCheatFiles(id6, txt, sel);
        nl::CheatModel m(c, txt, sel, NL_WII_CHEATS != 0);
        if (m.status() == nl::CheatStatus::Ready && m.skipped() > 0)
            nl::log::line("NL_CHEAT_SKIP %u", static_cast<unsigned>(m.skipped()));
        return m;
    };
    auto openCheats = [&](const nl::Game& g) {
        const std::string id6 = g.key != nullptr ? g.key : "";
        bool exists = false;
        nl::CheatModel m = loadCheatModel(g.console, id6, exists);
        std::optional<nl::ui::Str> blocked;
        if (cheatSourceTmpl.empty()) blocked = nl::ui::Str::CheatsNoSource;
        else if (!data.writable) blocked = nl::ui::Str::CheatsReadOnly;
        const unsigned rows = static_cast<unsigned>(m.rows());
        cheatsScene.open(std::move(m), id6, g.title != nullptr ? g.title : id6, exists, blocked);
        if (!scenes.contains(&cheatsScene)) scenes.push(&cheatsScene);
        nl::log::line("NL_SCENE cheats rows=%u", rows);
    };
    auto saveCheats = [&]() {
        if (!cheatsScene.hasModel()) return;
        const std::string& id6 = cheatsScene.id6();
        const std::string dir = data.path("codes");
        if (data.writable && fs.makeDirs(dir).isOk() &&
            writeText(fs, nl::cheatSelPath(data.dir, id6).c_str(), cheatsScene.model().selText())) {
            nl::log::line("NL_CHEATS_SAVED %s n=%u", id6.c_str(), static_cast<unsigned>(cheatsScene.model().selection().size()));
        } else {
            nl::log::line("NL_WARN %s.sel nicht gespeichert", id6.c_str());
        }
    };
    auto pumpCheats = [&]() {
        for (const std::string& line : cheatWorker.takeLogLines()) nl::log::line("%s", line.c_str());
        if (const auto r = cheatWorker.takeResult()) {
            nl::log::line("NL_CHEAT_FETCH %.*s", static_cast<int>(nl::cheatFetchTag(*r).size()), nl::cheatFetchTag(*r).data());
            // Die Szene zeigt noch dasselbe Spiel: Ergebnis melden, bei Erfolg neu einlesen.
            if (cheatsScene.hasModel() && cheatsScene.fetching() && cheatsScene.id6() == cheatFetchId6) {
                std::optional<nl::CheatModel> reloaded;
                if (*r == nl::CheatFetchResult::Ok || *r == nl::CheatFetchResult::Exists) {
                    bool exists = false;
                    reloaded.emplace(loadCheatModel(cheatsConsole, cheatFetchId6, exists));
                }
                cheatsScene.fetchDone(*r, std::move(reloaded));
            }
        }
    };
    // Fortschritt, Marker und Ergebnis des Netz-Threads übernehmen (Haupt-Thread).
    auto pumpNet = [&]() {
        pumpCheats();
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
    enum class EmuJob : u8 { None, List, Install, SelfUpdate };
    EmuJob emuJob = EmuJob::None;
    bool restartPending = false;  // M20: neu starten, sobald kein Dialog und kein Fern-KEY mehr läuft
    int updateToastFrames = 0;
    std::string emuDebugPending = config.debugEmuInstall;  // Komma-Liste; nach dem Laden der Liste installieren
    bool autostartPending = false;                         // debug autostart wartet auf diese Installation
    auto refreshEmuRows = [&]() {
        const std::string text = emuRoot.empty() ? std::string() : readText(fs, nl::installedEmusPath(emuRoot).c_str());
        emuScene.setRows(nl::emuRows(nl::parseInstalledEmus(text),
                                     [&](std::string_view dol) { return nl::fileSize(fs, nl::onRoot(dol, emuRoot)).has_value(); },
                                     emuList));
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
        if (apps.empty() || emuRoot.empty() || emu.running()) return;
        if (!emu.startInstall(fs, emuRepo, std::move(apps), emuRoot)) {
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
            if (emuJob == EmuJob::SelfUpdate && updateToastFrames-- <= 0) {
                char buf[96];
                const u32 total = emu.total(), done = emu.done();
                if (emu.step() == nl::EmuStep::Download && total > 0)
                    std::snprintf(buf, sizeof buf, nl::ui::tr(nl::ui::Str::UpdateProgress),
                                  static_cast<unsigned>(u64{done} * 100 / total));
                else
                    std::snprintf(buf, sizeof buf, "%s", nl::ui::tr(nl::ui::Str::UpdateChecking));
                mainScene.showToast(buf);
                updateToastFrames = 30;
            }
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
        if (finished == EmuJob::SelfUpdate) {
            const nl::SelfUpdateResult r = emu.takeSelfUpdate().value_or(nl::SelfUpdateResult{});
            char text[256];
            switch (r.outcome) {
                case nl::SelfUpdateOutcome::UpToDate:
                    std::snprintf(text, sizeof text, nl::ui::tr(nl::ui::Str::UpdateCurrent), NL_VERSION);
                    break;
                case nl::SelfUpdateOutcome::Installed:
                    std::snprintf(text, sizeof text, nl::ui::tr(nl::ui::Str::UpdateInstalled), r.version.c_str());
                    restartPending = true;
                    break;
                case nl::SelfUpdateOutcome::NoManifest:
                    std::snprintf(text, sizeof text, "%s", nl::ui::tr(nl::ui::Str::UpdateNoManifest));
                    break;
                case nl::SelfUpdateOutcome::BadManifest:
                    std::snprintf(text, sizeof text, nl::ui::tr(nl::ui::Str::UpdateFailed), "update.ini");
                    break;
                case nl::SelfUpdateOutcome::Failed:
                    std::snprintf(text, sizeof text, nl::ui::tr(nl::ui::Str::UpdateFailed),
                                  std::string(nl::emuInstallErrorName(r.install.error)).c_str());
                    break;
            }
            dialog.open(nl::ui::tr(nl::ui::Str::UpdateTitle), text);
            scenes.push(&dialog);
        } else if (finished == EmuJob::List) {
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
            } else if (std::vector<nl::OscApp> pending = emuScene.takePendingTargets(); !pending.empty()) {
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(nl::ui::AppCommand::EmuInstallAll));
                startEmuInstall(std::move(pending));
            }
        } else {
            emuScene.setBusy(std::nullopt);
            refreshEmuRows();
            detectEmulators();
            probeN64Dols();
            nl::log::line("NL_EMU_DONE not64=%d wii64=%d", not64Dol ? 1 : 0, wii64Dol ? 1 : 0);
            onDebugInstallDone();
        }
        if (data.writable) nl::log::flushLogFile(fs, logPath);
        if (perf) perf->pause();
    };

    // M20: Update aus dem GitHub-Release ([debug] update_url für den Smoke). Läuft als EmuWorker-Auftrag.
    auto startSelfUpdate = [&]() {
        const std::string url =
            config.debugUpdateUrl.empty() ? std::string(nl::kDefaultUpdateUrl) : config.debugUpdateUrl;
        if (emu.running() || net.running() || !emu.startSelfUpdate(fs, url, NL_VERSION)) {
            nl::log::line("NL_SELF_UPDATE busy");
            dialog.open(nl::ui::tr(nl::ui::Str::UpdateTitle), nl::ui::tr(nl::ui::Str::UpdateBusy));
            scenes.push(&dialog);
            return;
        }
        emuJob = EmuJob::SelfUpdate;
        updateToastFrames = 0;
    };

    // debug autostart (nur für Tests); wartet ggf. auf die Debug-Emulator-Installation.
    // Spieloptionen (Spec 7): pro Spiel (g != nullptr) oder global für eine Konsole.
    auto openOptions = [&](nl::Console c, const nl::Game* g) {
        std::optional<std::string> gamesText =
            data.readable ? nl::optionsFileText(nl::readAllWithFallback(fs, gamesPath, kMaxIniBytes)) : std::string();
        if (!gamesText) {  // unlesbar (zu groß, Kartenfehler): nicht öffnen, sonst ginge der Rest beim Speichern verloren
            nl::log::line("NL_WARN games.ini nicht lesbar, Optionen nicht geöffnet");
            audio.play(nl::UiSound::Error);
            return;
        }
        std::optional<nl::NinVersion> nin;  // unbekannt: nichts gesperrt
        if (c == nl::Console::Gc && !emulators.nintendont().dol.empty())
            nin = nl::findNintendontVersion(fs, emulators.nintendont().dol);
        const bool isVWii =
            nl::platformForSysMenu(nl::platform::sysMenuVersion(), nl::platform::latteDetected()) == nl::Platform::VWii;
        optionsScene.open(nl::OptionsModel(c, g != nullptr ? std::string(g->key) : std::string(), configText,
                                           std::move(*gamesText), nin, isVWii),
                          g != nullptr && g->title != nullptr ? std::string(g->title) : std::string());
        scenes.push(&optionsScene);
        nl::log::line("NL_SCENE options");
    };
    auto saveOptions = [&]() {
        if (!optionsScene.hasModel()) return;
        const nl::OptionsModel& m = optionsScene.model();
        if (m.perGame()) {
            if (data.writable && writeText(fs, gamesPath.c_str(), m.gamesIniText())) {
                overrides = nl::GameOverrides::fromIni(nl::IniDoc::parse(m.gamesIniText()));
                nl::log::line("NL_OPTIONS_SAVED games.ini %s", m.id6().c_str());
            } else {
                nl::log::line("NL_WARN games.ini nicht gespeichert");
            }
        } else if (data.writable && writeText(fs, configPath.c_str(), m.configIniText())) {
            configText = m.configIniText();
            // [wii] ios gehört auch zur Live-Config; sonst schriebe saveConfig den alten Wert zurück.
            config.wiiIos = nl::parseIosValue(nl::IniDoc::parse(configText).get("wii", "ios"));
            nl::log::line("NL_OPTIONS_SAVED config.ini %s", m.console() == nl::Console::Gc ? "gc" : "wii");
        } else {
            nl::log::line("NL_WARN config.ini nicht gespeichert");
        }
    };
    // PIN-Abfrage: Zweck merken, PIN prüfen (3 Fehler → 30 s Pause), danach den Zweck ausführen.
    enum class PinPurpose : u8 { Launch, Parental, Setup };
    PinPurpose pinPurpose = PinPurpose::Launch;
    std::string pinGameKey, pendingLaunchKey;
    nl::PinUnlock pinUnlock;  // Freigabe für den Neuversuch nach dem Preflight-Hinweis (einmal, kurz, dasselbe Spiel)
    auto launchByKey = [&](const std::string& key) {
        for (const nl::Game& g : store.games())
            if (key == g.key) {
                launchGame(g);
                return;
            }
        nl::log::line("NL_WARN %s fehlt", key.c_str());
    };
    auto submitPin = [&](std::string_view pin) {
        const u32 now = msSince(bootTicks);
        if (pinGate.blocked(now)) {
            nl::log::line("NL_PARENTAL blocked");
            return false;
        }
        const bool ok = pinGate.submit(nl::checkPin(parental, pin), now);
        nl::log::line(ok ? "NL_PARENTAL ok" : "NL_PARENTAL denied");
        if (ok && config.parental.lockUntil != 0) {  // richtige PIN: keine Sperre mehr für den nächsten Start
            config.parental.lockUntil = 0;
            saveConfig();
        }
        if (!ok && pinGate.blocked(now)) {  // dritter Fehler: Sperrende merken, damit ein Neustart sie nicht aufhebt
            if (const std::time_t t = std::time(nullptr); t > 0) {
                config.parental.lockUntil = static_cast<u32>(t) + 30u;
                saveConfig();
            }
        }
        return ok;
    };
    // fromScene: die PIN-Szene ist offen und schließt erst im nächsten Frame; Folgeszenen öffnen danach.
    auto runPinPurpose = [&](bool fromScene) {
        switch (pinPurpose) {
            case PinPurpose::Launch:
                pinUnlock.grant(pinGameKey, msSince(bootTicks));
                if (fromScene) {
                    pendingLaunchKey = pinGameKey;  // startet, sobald die PIN-Szene weg ist
                    pinScene.accept();
                } else {
                    launchByKey(pinGameKey);
                }
                break;
            case PinPurpose::Parental:
                if (fromScene) pinScene.accept(&parentalScene);
                else scenes.push(&parentalScene);
                break;
            case PinPurpose::Setup:
                openSetup();
                if (fromScene) pinScene.accept(&setupScene);
                else scenes.push(&setupScene);
                break;
        }
    };
    auto askPin = [&](PinPurpose purpose, std::string gameKey) {
        pinPurpose = purpose;
        pinGameKey = std::move(gameKey);
        if (!config.debugPinInput.empty()) {  // nur für Tests (M17): PIN ohne Szene beantworten
            if (submitPin(config.debugPinInput)) runPinPurpose(false);
            return;
        }
        pinScene.open(nl::ui::PinMode::Verify);
        // HOME öffnet das HOME-Menü nur dort, wo es das ohne Abfrage auch täte: direkt über der Spieleliste.
        pinScene.setHomeMenu(scenes.top() == &mainScene ? &homeMenu : nullptr);
        scenes.push(&pinScene);
        nl::log::line("NL_PARENTAL ask");
    };
    auto requestLaunch = [&](const nl::Game& game) {
        // Zweites A nach dem Preflight-Hinweis: dieselbe Freigabe gilt noch, keine zweite PIN.
        const bool retry = pinUnlock.consume(game.key, msSince(bootTicks)) && warnedLaunch == game.key;
        if (mainScene.isGameLocked(game) && !retry) askPin(PinPurpose::Launch, game.key);
        else launchGame(game);
    };
    auto runPendingAutostart = [&]() {
        if (!autostartPending) return;
        autostartPending = false;
        const std::string& key = config.debugAutostart;
        const nl::Game* found = nullptr;
        for (const nl::Game& g : store.games()) {
            if (key == g.key) {
                found = &g;
                break;
            }
        }
        if (found != nullptr) {
            nl::log::line("NL_AUTOSTART %s", key.c_str());
            requestLaunch(*found);
        } else {
            nl::log::line("NL_WARN autostart %s fehlt", key.c_str());
        }
    };

    nl::ui::DebugOverlay debug;
    nl::FrameStats stats;
    bool readyReported = false;
    bool coversReported = false;
#ifndef NL_SELFTEST
    bool wifiProbeStarted = false;
    u64 nextWifiProbeMs = 0;
#endif
    bool running = true;

    // M20: Fernsteuerung. Der Socket-Thread liefert höchstens eine Anfrage gleichzeitig; hier wird sie ausgeführt.
    u64 remoteKeyBits = 0;
    u32 remoteKeyFrames = 0, remoteKeyRelease = 0;
    auto replyAndWait = [&](bool ok, std::string info) {  // vor Start/Neustart/Ende: Antwort muss raus sein
        const u32 before = remote.sentCount();
        remote.reply(ok, std::move(info));
        for (int i = 0; i < 200 && remote.sentCount() == before; ++i) usleep(10 * 1000);
    };
    auto pumpRemoteLog = [&]() {
        for (const std::string& line : remote.takeLogLines()) nl::log::line("%s", line.c_str());
    };
    // fromRemote: Antwort an den Client; die Fernsteuerung bleibt nach dem Neustart an, wenn sie jetzt läuft.
    auto restartSelf = [&](bool fromRemote) {
        std::vector<std::string> extra;
        if (remote.running()) extra.push_back("--remote");
        auto plan = nl::preflightDol(fs, selfDol, extra);
        if (!plan.isOk()) {
            nl::log::line("NL_WARN Neustart: %s %s", selfDol.c_str(), plan.error().context.c_str());
            if (fromRemote) {
                remote.reply(false, selfDol + ": " + plan.error().context);
            } else {
                audio.play(nl::UiSound::Error);
            }
            return;
        }
        nl::log::line("NL_RESTART %s", selfDol.c_str());
        if (fromRemote) replyAndWait(true, selfDol);
        worker.cancelAndJoin();
        discDrive.shutdown();
        net.join();
        emu.join();
        cheatWorker.join();
        remote.stop();
        pumpRemoteLog();
        netService.shutdown();
        covers.shutdown();
        banners.shutdown();
        if (auto loaded = nl::launch::loadDolToBuffer(fs, plan.value()); !loaded.isOk()) {
            nl::log::line("NL_WARN Neustart: %s", loaded.error().context.c_str());
            running = false;  // Netz ist schon unten: sauber zum HBC zurück
            return;
        }
        stopAudio();
        logPerfSummary();
        if (data.writable) nl::log::flushLogFile(fs, logPath);
        input.shutdown();
        nl::platform::unmountAll();
        nl::platform::shutdownVideo();
        nl::launch::chainload(plan.value());
    };
    auto handleRemote = [&]() {
        if (!remote.running() || remoteKeyFrames > 0 || remoteKeyRelease > 0) return;
        nl::RemoteRequest rq;
        std::vector<u8> body;
        if (!remote.take(rq, body)) return;
        auto textBytes = [](const std::string& s) { return std::vector<u8>(s.begin(), s.end()); };
        switch (rq.cmd) {
            case nl::RemoteCmd::Hello:
                remote.reply(true, std::string("newlauncher ") + NL_VERSION);
                break;
            case nl::RemoteCmd::Log:
                remote.reply(true, "", nl::log::snapshot());
                break;
            case nl::RemoteCmd::Shot: {  // XFB ist ungecacht (video.cpp): direkt lesbar
                const GXRModeObj* m = video.mode();
                const u8* fb = static_cast<const u8*>(video.shownFramebuffer());
                if (m == nullptr || fb == nullptr) {
                    remote.reply(false, "kein Framebuffer");
                    break;
                }
                const std::size_t bytes = static_cast<std::size_t>(m->fbWidth) * m->xfbHeight * 2;
                remote.reply(true, std::to_string(m->fbWidth) + " " + std::to_string(m->xfbHeight) + " yuyv",
                             std::vector<u8>(fb, fb + bytes));
                break;
            }
            case nl::RemoteCmd::State: {
                std::string s = std::string("version\t") + NL_VERSION + "\nfocus\t" + mainScene.focusedKey() +
                                "\ndepth\t" + std::to_string(scenes.depth()) + "\ngames\t" +
                                std::to_string(store.games().size()) + "\nwifi\t" +
                                std::to_string(static_cast<int>(ctx.wifi)) + "\ndata\t" + data.dir + "\nself\t" +
                                selfDol + "\n";
                remote.reply(true, "", textBytes(s));
                break;
            }
            case nl::RemoteCmd::Games: {
                std::string s;
                for (const nl::Game& g : store.games()) {
                    s += g.key;
                    s += '\t';
                    s += nl::consoleInfo(g.console).key;
                    s += '\t';
                    s += g.title != nullptr ? g.title : "";
                    s += '\n';
                }
                remote.reply(true, std::to_string(store.games().size()), textBytes(s));
                break;
            }
            case nl::RemoteCmd::Key:  // Antwort nach dem Loslassen (oben in der Schleife)
                remoteKeyBits = rq.buttons;
                remoteKeyFrames = rq.frames;
                remoteKeyRelease = 2;
                break;
            case nl::RemoteCmd::Launch: {
                const nl::Game* found = nullptr;
                for (const nl::Game& g : store.games()) {
                    if (rq.text == g.key) {
                        found = &g;
                        break;
                    }
                }
                if (found == nullptr) {
                    remote.reply(false, "Spiel unbekannt: " + rq.text);
                    break;
                }
                nl::log::line("NL_REMOTE launch %s", rq.text.c_str());
                replyAndWait(true, rq.text);  // Ergebnis zeigen LOG/STATE; ein Start beendet die Verbindung
                requestLaunch(*found);
                break;
            }
            case nl::RemoteCmd::Put: {
                if (!nl::remotePutAllowed(rq.text, data.dir)) {
                    remote.reply(false, "Pfad nicht erlaubt");
                    break;
                }
                const std::string dir = rq.text.substr(0, rq.text.rfind('/'));
                const bool ok = fs.makeDirs(dir).isOk() && fs.writeAtomic(rq.text, body).isOk();
                nl::log::line("NL_REMOTE put %s bytes=%u %s", rq.text.c_str(), static_cast<unsigned>(body.size()),
                              ok ? "ok" : "fail");
                remote.reply(ok, ok ? std::to_string(body.size()) : std::string("Schreiben fehlgeschlagen"));
                break;
            }
            case nl::RemoteCmd::Restart:
                restartSelf(true);
                break;
            case nl::RemoteCmd::Exit:
                nl::log::line("NL_REMOTE exit");
                replyAndWait(true, "");
                running = false;
                break;
        }
    };

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
        nl::ui::InputFrame in = frame.begin(nl::app::selftestInput(frameNo, view));
#else
        nl::ui::RawInput raw = input.poll();
        if (remoteKeyFrames > 0) {  // M20: KEY hält die Tasten N Bilder lang, dann zwei Bilder loslassen
            raw.held |= remoteKeyBits;
            --remoteKeyFrames;
        } else if (remoteKeyRelease > 0 && --remoteKeyRelease == 0) {
            if (restartPending)
                replyAndWait(true, "");  // Antwort muss vor dem Neustart raus sein
            else
                remote.reply(true, "");
        }
        nl::ui::InputFrame in = frame.begin(raw);
#endif
        pumpRemoteLog();
        if (readyReported) handleRemote();
        if (restartPending && !scenes.contains(&dialog) && remoteKeyFrames == 0 && remoteKeyRelease == 0) {
            restartPending = false;  // M20: nach „Neu starten“ oder dem Update-Dialog
            restartSelf(false);
        }
        if (detailsBackAt != 0 && gettime() >= detailsBackAt) {  // nur für Tests (M18): [debug] details_back_ms
            detailsBackAt = 0;
            if (scenes.top() == &details) {
                in.actions |= 1u << static_cast<u32>(nl::ui::Action::Back);
                nl::log::line("NL_DEBUG details back");
            }
        }
        ctx.battery = input.battery();
        ctx.wifi = netService.wifiStatus();
        // Ergebnis des Hintergrund-Scans übernehmen (Spec 6.1 Schritt 6)
        if (auto result = worker.take()) {
            if (result->isOk()) {
                const nl::ScanStats st = result->value().stats;
                logWbfsSkips(result->value());
                const std::string focusKey = mainScene.focusedKey();  // vor setGames: danach sind die Zeiger ungültig
                deviceIndexes = std::move(*result).value().indexes;
                applyIndexes();
                covers.setRoots(coverRootsOf(store.devices()));
                covers.libraryChanged();
                banners.setRoots(bannerRootsOf(store.devices()));
                mainScene.setLibrary(libraryView(), focusKey);
                nl::log::line("NL_SCAN_DONE games=%u added=%u removed=%u", static_cast<unsigned>(store.games().size()),
                              static_cast<unsigned>(st.added), static_cast<unsigned>(st.removed));
                saveIndexes();
            } else {
                nl::log::line("NL_WARN Scan fehlgeschlagen: %s", result->error().context.c_str());
            }
        }
        bool discManual = false;
        if (auto discRes = discDrive.poll(&discManual)) applyDiscResult(*discRes, discManual);

        if (setupScene.netStatus() == nl::ui::SetupScene::NetTest::Running && setupNet != 0) {
            const int r = setupNet;
            setupScene.setNetStatus(r == 1   ? nl::ui::SetupScene::NetTest::Ok
                                    : r == 2 ? nl::ui::SetupScene::NetTest::OkTimeSet
                                             : nl::ui::SetupScene::NetTest::Fail);
            nl::log::line("NL_WIZARD net %s", r == 3 ? "fail" : "ok");
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
                // Auf dem Dateitext statt über toIni(): Kommentare und Schreibweisen der übrigen Abschnitte bleiben.
                const std::optional<std::string> gamesText =
                    data.readable ? nl::optionsFileText(nl::readAllWithFallback(fs, gamesPath, kMaxIniBytes)) : std::string();
                std::string text = gamesText ? nl::setN64MethodText(*gamesText, key, method) : std::string();
                if (gamesText && data.writable && writeText(fs, gamesPath.c_str(), text)) {
                    overrides = nl::GameOverrides::fromIni(nl::IniDoc::parse(text));
                    nl::log::line("NL_OVERRIDE_SAVED %s %s", key.c_str(), method ? nl::n64MethodKey(*method) : "auto");
                } else {
                    overrides.setN64Method(key, method);  // gilt bis zum Neustart, auch ohne Datei
                    nl::log::line("NL_WARN games.ini nicht gespeichert");
                }
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
            case nl::ui::AppCommand::ToggleCategory: {
                const auto names = categories.names();
                if (req.game == nullptr || req.category >= names.size()) break;
                const std::string key = req.game->key;
                const std::string name = names[req.category];
                const bool on = !categories.contains(name, key);
                if (!categories.set(name, key, on)) {
                    nl::log::line("NL_WARN Kategorie nicht geändert");
                    break;
                }
                if (data.writable && !categoriesUnreadable && writeText(fs, categoriesPath.c_str(), categories.toText()))
                    nl::log::line("NL_CAT_SAVED %s %s %d", name.c_str(), key.c_str(), on ? 1 : 0);
                else nl::log::line("NL_WARN categories.ini nicht gespeichert");
                mainScene.syncDetailsCategories(key);
                mainScene.refreshFilter();
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
                    banners.setGerman(lang == nl::ui::Lang::De);  // Detailansicht lädt ihr Banner danach neu
                    channelsLoaded = false;  // Kanalnamen beim nächsten Öffnen in der neuen Sprache laden
                    scanEmuNandChannels();
                    mainScene.setLibrary(libraryView(), mainScene.focusedKey());
                    nl::log::line("NL_LANG %s", nl::ui::langKey(lang));
                }
                ctx.themeMode = config.themeMode;
                theme.setTarget(nl::isDarkAt(ctx.themeMode, ctx.minuteOfDay, config.darkFromMin, config.darkToMin), true);
                if (config.themePack != appliedThemePack) applyThemePack();
                mainScene.setSort(config.sortOrder, &playStats);
                audio.setSounds(config.sounds);
                banners.setEnabled(config.banners);
                music.setEnabled(config.music);
                music.setVolume(config.musicVolume);
                if (config.music) startMusic();  // beim Start ausgeschaltet gewesen
                refreshParental();
                break;
            }
            case nl::ui::AppCommand::SaveSettings:
                config.themeMode = ctx.themeMode;
                saveConfig();
                refreshParental();
                break;
            case nl::ui::AppCommand::OpenParental:
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                if (parental.enabled) askPin(PinPurpose::Parental, {});
                else scenes.push(&parentalScene);
                break;
            case nl::ui::AppCommand::ChangePin:
                pinScene.open(nl::ui::PinMode::Set);
                scenes.push(&pinScene);
                break;
            case nl::ui::AppCommand::RemovePin:
                config.parental = nl::withoutPin(config.parental);
                saveConfig();
                refreshParental();
                nl::log::line("NL_PARENTAL removed");
                break;
            case nl::ui::AppCommand::PinEntered:  // kein NL_CMD: nichts von der Eingabe ins Log
                if (pinScene.mode() == nl::ui::PinMode::Set) {
                    std::array<u8, 16> salt{};
                    nl::platform::randomBytes(salt);
                    config.parental = nl::withPin(config.parental, pinScene.pinText(), salt);
                    saveConfig();
                    refreshParental();
                    nl::log::line("NL_PARENTAL pin_set");
                    pinScene.accept();
                } else if (submitPin(pinScene.pinText())) {
                    runPinPurpose(true);
                } else {
                    pinScene.reject(pinGate.blocked(msSince(bootTicks)) ? nl::ui::Str::PinBlocked : nl::ui::Str::PinWrong);
                    audio.play(nl::UiSound::Error);
                }
                break;
            case nl::ui::AppCommand::Rescan:
                discDrive.requestRead(false);
                scanEmuNandChannels();
                mainScene.setLibrary(libraryView(), mainScene.focusedKey());
                if (worker.running()) break;  // läuft schon, das Ergebnis kommt über den Scan-Pfad unten
                if (!worker.start(fs, libDevices, scanRules, {}, metaPtr)) {
                    nl::log::line("NL_WARN Neu-Scan nicht gestartet");
                    break;
                }
                mainScene.showToast(nl::ui::tr(nl::ui::Str::Rescanning));
                nl::log::line("NL_RESCAN start");
                break;
            case nl::ui::AppCommand::ReadDisc:
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                if (discDrive.requestRead(true)) mainScene.showToast(nl::ui::tr(nl::ui::Str::ReadingDisc));
                break;
            case nl::ui::AppCommand::ExitToHbc:  // Spec 6.8: aufräumen, exit(0)
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                running = false;
                break;
            case nl::ui::AppCommand::WiiMenu:
            case nl::ui::AppCommand::PowerOff:
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                worker.cancelAndJoin();  // erst den Scan beenden, dann Laufwerke aushängen
                discDrive.shutdown();
                net.join();
                emu.join();
                cheatWorker.join();
                remote.stop();  // vor dem Netz: der Fernsteuerungs-Thread nutzt dessen Sockets
                pumpRemoteLog();
                netService.shutdown();
                covers.shutdown();
                banners.shutdown();
                stopAudio();
                logPerfSummary();
                if (data.writable) nl::log::flushLogFile(fs, logPath);
                input.shutdown();
                nl::platform::unmountAll();
                SYS_ResetSystem(cmd == nl::ui::AppCommand::WiiMenu ? SYS_RETURNTOMENU : SYS_POWEROFF, 0, 0);
                break;
            case nl::ui::AppCommand::Launch:
                if (req.game != nullptr) requestLaunch(*req.game);
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
            case nl::ui::AppCommand::OpenOptions:
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                openOptions(req.game != nullptr ? req.game->console : req.console, req.game);
                break;
            case nl::ui::AppCommand::SaveOptions:
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                saveOptions();
                break;
            case nl::ui::AppCommand::OpenCheats:
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                if (req.game != nullptr) {
                    cheatsConsole = req.game->console;
                    openCheats(*req.game);
                }
                break;
            case nl::ui::AppCommand::FetchCheats:
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                if (!startCheatFetch(cheatsScene.id6(), cheatsScene.overwrite()))
                    cheatsScene.fetchDone(nl::CheatFetchResult::NetError, std::nullopt);
                break;
            case nl::ui::AppCommand::SaveCheats:
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                saveCheats();
                break;
            case nl::ui::AppCommand::SelfUpdate:
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                startSelfUpdate();
                break;
            case nl::ui::AppCommand::RestartSelf:
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                restartPending = true;
                break;
            case nl::ui::AppCommand::OpenAbout:
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                scenes.push(&aboutScene);
                nl::log::line("NL_SCENE about");
                break;
            case nl::ui::AppCommand::OpenSysInfo:
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                openSysInfo();
                break;
            case nl::ui::AppCommand::SaveSysInfo:
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                saveSysInfo();
                break;
            case nl::ui::AppCommand::OpenSetup:
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                if (parental.enabled) {  // Spec v2 10.5: Einrichtung nur mit PIN
                    askPin(PinPurpose::Setup, {});
                    break;
                }
                openSetup();
                scenes.push(&setupScene);
                break;
            case nl::ui::AppCommand::SetupNetTest:
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                startSetupNetTest();
                break;
            case nl::ui::AppCommand::FinishSetup:
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                finishSetup();
                break;
            case nl::ui::AppCommand::EmuCancel:
                nl::log::line("NL_CMD %s", nl::ui::appCommandName(cmd));
                emu.cancel();
                break;
            case nl::ui::AppCommand::None:
                break;
        }
        if (!running) break;
        if (!pendingLaunchKey.empty() && !scenes.contains(&pinScene)) {  // PIN angenommen, Szene ausgeblendet
            const std::string key = std::move(pendingLaunchKey);
            pendingLaunchKey.clear();
            launchByKey(key);
        }
        pumpNet();
        pumpEmu(runPendingAutostart);
        pumpMusic();
        covers.pump();  // nach update (Wünsche dieses Frames), vor draw; Freigaben erst nach GX_DrawDone des Vorframes
        if (scenes.top() == &details)
            banners.pump();
        else
            banners.release();  // Detailansicht verlassen oder überdeckt: Ton aus, Musik laut, Texturen frei

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
            if (!config.debugFakeDisc.empty()) applyDiscResult(discDrive.readSync(), false);
            else discDrive.requestRead(false);
            if (config.music) startMusic();
            // Nach der Rückkehr über den Stub nicht erneut starten (Smoke --return-stub liefe sonst im Kreis).
            autostartPending = !config.debugAutostart.empty() && !stubAtBoot;
            if (stubAtBoot && !config.debugAutostart.empty()) nl::log::line("NL_AUTOSTART skip return_stub");
            if (!emuDebugPending.empty()) startEmuList();  // nur für Tests; Autostart wartet auf die Installation
            if (emuJob == EmuJob::None) {
                emuDebugPending.clear();  // Thread startet nicht: nichts zu warten
                runPendingAutostart();
            }
            if (config.debugFetchCovers) fetchCovers();  // nur für Tests
            if (config.debugSelfUpdate && emuJob == EmuJob::None) startSelfUpdate();  // nur für Tests (M20)
            if (!config.debugCheatFetch.empty() && !startCheatFetch(config.debugCheatFetch, false))  // nur für Tests (M15)
                nl::log::line("NL_CHEAT_FETCH nosource");
            if (!config.debugFilter.empty()) {  // nur für Tests (M17)
                if (const auto f = nl::parseFilter(config.debugFilter))
                    nl::log::line("NL_FILTER %s n=%d bar=%s", nl::filterSpec(*f).c_str(), mainScene.setFilter(*f),
                                  mainScene.barLabelText(mainScene.barIndex()));
                else nl::log::line("NL_FILTER bad %s", config.debugFilter.c_str());
            }
            if (config.debugOpenScene == "about") {   // nur für Tests (M11)
                scenes.push(&aboutScene);
                nl::log::line("NL_SCENE about");
            }
            if (config.debugOpenScene == "sysinfo") openSysInfo();  // nur für Tests (M13)
            if (config.debugOpenScene.rfind("details:", 0) == 0) {  // nur für Tests (M18): details:<ID6>
                const std::string id = config.debugOpenScene.substr(8);
                const nl::Game* found = nullptr;
                for (const nl::Game& g : store.games())
                    if (id == g.key) {
                        found = &g;
                        break;
                    }
                if (found != nullptr) {
                    details.open(*found, store.isFavorite(found->key), 0);
                    scenes.push(&details);
                    refreshDetailsN64();
                    nl::log::line("NL_SCENE details %s", id.c_str());
                    if (config.debugDetailsBackMs > 0)
                        detailsBackAt = gettime() + millisecs_to_ticks(static_cast<u64>(config.debugDetailsBackMs));
                } else {
                    nl::log::line("NL_WARN details %s fehlt", id.c_str());
                }
            }
            if (config.debugOpenScene.rfind("cheats:", 0) == 0) {  // nur für Tests (M15): cheats:<ID6>
                const std::string id = config.debugOpenScene.substr(7);
                const nl::Game* found = nullptr;
                for (const nl::Game& g : store.games())
                    if (id == g.key) {
                        found = &g;
                        break;
                    }
                if (found != nullptr) {
                    cheatsConsole = found->console;
                    openCheats(*found);
                } else {
                    nl::log::line("NL_WARN cheats %s fehlt", id.c_str());
                }
            }
            if (config.debugOpenScene.rfind("options:", 0) == 0) {  // nur für Tests (M14): options:<ID6>|wii|gc
                const std::string id = config.debugOpenScene.substr(8);
                const nl::Game* found = nullptr;
                for (const nl::Game& g : store.games())
                    if (id == g.key) {
                        found = &g;
                        break;
                    }
                if (found != nullptr) openOptions(found->console, found);
                else if (id == "wii" || id == "gc") openOptions(id == "gc" ? nl::Console::Gc : nl::Console::Wii, nullptr);
                else nl::log::line("NL_WARN options %s fehlt", id.c_str());
                const std::size_t eq = config.debugOptionSet.find('=');
                if (scenes.contains(&optionsScene) && eq != std::string::npos) {
                    const std::string k = config.debugOptionSet.substr(0, eq), v = config.debugOptionSet.substr(eq + 1);
                    const bool ok = optionsScene.model().set(k, v);
                    nl::log::line("NL_OPTION_SET %s=%s %s", k.c_str(), v.c_str(), ok ? "ok" : "fail");
                    optionsScene.requestClose();
                }
            }
            if (config.debugSysinfoSave) {                          // nur für Tests (M13)
                sysInfoScene.setInfo(buildSysInfo());
                saveSysInfo();
            }
            if (firstRun) {  // Spec v2 6.4: ohne config.ini startet der Einrichtungs-Assistent
                openSetup();
                if (config.debugWizard == "accept") {  // nur für Tests: jeden Vorschlag annehmen, ohne Netztest
                    for (int i = 0; i < nl::kWizardStepCount && setupScene.wizard().step() != nl::WizardStep::Done; ++i)
                        setupScene.wizard().accept();
                    finishSetup();
                } else {
                    scenes.push(&setupScene);
                }
            }
        }
        if (readyReported && !coversReported && covers.idle()) {
            nl::log::line("NL_COVERS ready=%u", static_cast<unsigned>(covers.readyCount()));
            if (data.writable) nl::log::flushLogFile(fs, logPath);
            if (perf) perf->pause();
            coversReported = true;
        }
#ifndef NL_SELFTEST
        if (readyReported && !netService.busy()) {
            const u64 nowMs = msSince(bootTicks);
            if (!wifiProbeStarted || nowMs >= nextWifiProbeMs) {
                wifiProbeStarted = true;
                netService.startWifiProbe();
                nextWifiProbeMs = nowMs + (ctx.wifi == nl::WifiStatus::Connected ? 5000u : 15000u);
            }
        }
#endif
#ifdef NL_SELFTEST
        nl::app::selftestAfterFrame(frameNo, view);
#endif
    }
    worker.cancelAndJoin();  // erst den Scan beenden, dann Laufwerke aushängen
    discDrive.shutdown();
    net.join();
    emu.join();
    cheatWorker.join();
    remote.stop();  // vor dem Netz: der Fernsteuerungs-Thread nutzt dessen Sockets
    pumpRemoteLog();
    netService.shutdown();
    covers.shutdown();
    banners.shutdown();
    banners.release();
    stopAudio();
    logPerfSummary();
    if (data.writable) nl::log::flushLogFile(fs, logPath);
    input.shutdown();
    nl::platform::unmountAll();
    // M20 (Spec 4.4): Unser Stub würde bei exit() newlauncher neu laden; den HBC dann als Titel starten.
    if (nl::launch::returnStubInstalled()) {
        const u32 hbc = nl::hbcReturnTo(installedTitles);
        nl::log::line("NL_RETURN_STUB exit hbc=%08x", static_cast<unsigned>(hbc));
        nl::platform::shutdownVideo();
        if (hbc != 0) static_cast<void>(nl::launch::launchChannel(0x0001000100000000ull | hbc));
        SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
    }
    return 0;
}
