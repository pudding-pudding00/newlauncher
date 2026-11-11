#pragma once
// config.ini (Spec 3.4/5.4): Einstellungen mit Standardwerten; ungültige Werte fallen auf Standard.

#include <string>
#include <string_view>

#include "core/cover_download.hpp"
#include "core/ini.hpp"
#include "core/theme.hpp"

namespace nl {

struct Config {
    enum class Language : u8 { Auto, De, En };
    enum class LibraryDevice : u8 { Usb, Sd };

    Language language = Language::Auto;
    LibraryDevice library = LibraryDevice::Usb;
    int usbTimeoutSec = 15;
    ThemeMode themeMode = ThemeMode::Light;
    int darkFromMin = 19 * 60, darkToMin = 7 * 60;
    bool music = true;
    int musicVolume = 60;
    bool sounds = true;
    std::string coverRegion = "auto";
    std::string emulatorRepo = "http://hbb1.oscwii.org/hbb";  // [emulators] repo (Spec 6.9), http:// oder https://
    std::string coverBase = std::string(kGametdbCoverBase);   // [covers] base (M11): Cover-Quelle, http:// oder https://
    std::string debugAutostart;  // [debug] autostart: Spielschlüssel, der nach NL_READY startet
    std::string debugCoverUrl;   // [debug] cover_url: ersetzt die GameTDB-Basis (Smoke-Test)
    bool debugFetchCovers = false;  // [debug] fetch_covers = 1: „Fehlende Cover laden“ nach NL_READY
    bool debugPerf = false;         // [debug] perf = 1: NL_PERF-Zeilen für die Hardware-Messung (M7)
    bool debugIgnoreAhbprot = false;  // [debug] ignore_ahbprot = 1: AHBPROT-Prüfung für GC überbrücken (Dolphin-Smoke, M8)
    std::string debugTlsCa;         // [debug] tls_ca: PEM der Test-CA statt Mozilla-Bundle (Smoke, M11)
    std::string debugOpenScene;     // [debug] open_scene = about: Szene nach NL_READY öffnen (Smoke, M11)
    bool debugWiiDryRun = false;    // [debug] wii_dry_run = 1: Wii-Spiel nur prüfen (NL_WII_DRYRUN), nicht starten (M9)
    bool debugLogSounds = false;    // [debug] log_sounds = 1: NL_SOUND <name> je gespieltem Klang (Smoke-Test)
    std::string debugEmuRepo;       // [debug] emu_repo: ersetzt emulatorRepo (Smoke-Test)
    std::string debugEmuInstall;    // [debug] emu_install: Komma-Liste, nach NL_READY installieren (Smoke-Test)
};

Config configFromIni(const IniDoc& ini);
std::string defaultConfigText();
int parseClock(std::string_view hhmm, int fallback);  // "19:00" → 1140
std::string formatClock(int minuteOfDay);              // 1140 → "19:00"

// Schreibt alle Schlüssel aus Spec 5.4 in den bisherigen Dateitext. Kommentare, unbekannte Schlüssel,
// fremde Abschnitte (auch [debug]) und das Zeilenende (CRLF/LF) bleiben erhalten; fehlende Schlüssel
// kommen ans Ende ihres Abschnitts, fehlende Abschnitte ans Dateiende.
std::string configToIniText(const Config& c, std::string_view previousText);

}  // namespace nl
