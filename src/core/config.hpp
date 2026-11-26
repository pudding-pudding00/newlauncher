#pragma once
// config.ini (Spec 3.4/5.4): Einstellungen mit Standardwerten; ungültige Werte fallen auf Standard.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/cheat_source.hpp"
#include "core/cover_download.hpp"
#include "core/emu_catalog.hpp"
#include "core/ini.hpp"
#include "core/library_sources.hpp"
#include "core/parental.hpp"
#include "core/play_stats.hpp"
#include "core/theme.hpp"

namespace nl {

// M24: Cover (covers_src, cache/covers, missing.txt) auf der SD-Karte (Standard) oder auf dem Spielgerät.
enum class CoverStorage : u8 { Sd, Device };

// M27: aktiver Cloud-Anbieter ([cloud] provider = off|drive); genau einer.
enum class CloudProviderId : u8 { None, Drive };

struct Config {
    enum class Language : u8 { Auto, De, En };
    enum class LibraryDevice : u8 { Usb, Sd };

    Language language = Language::Auto;
    LibraryDevice library = LibraryDevice::Usb;  // Alt-Schlüssel [general] library; gilt bis M12 Task 6 für den Start
    SortOrder sortOrder = SortOrder::Title;      // [library] sort = title|year|recent|most (M17)
    LibraryRules libraryRules;                   // [library] sources, [paths], [extensions] (Spec v2 5.2–5.4)
    int usbTimeoutSec = 15;
    ThemeMode themeMode = ThemeMode::Light;
    int darkFromMin = 19 * 60, darkToMin = 7 * 60;
    std::string themePack;  // [theme] name (Spec v2 §16; M16-Schlüssel pack wird noch gelesen): Ordner unter <data>/themes, leer = Standard
    bool music = true;
    int musicVolume = 60;
    bool sounds = true;
    bool banners = true;  // [ui] banners: Kanal-/GC-Banner in der Detailansicht (M18)
    std::string coverRegion = "auto";
    CoverStorage coverStorage = CoverStorage::Sd;  // [covers] storage = sd|device (M24): Ort der Cover
    bool coverAuto = true;  // [covers] auto: fehlende Cover nach dem Scan selbst laden (M21)
    std::string emulatorRepo = std::string(defaultEmuRepo(true));  // [emulators] repo (Spec 6.9), http:// oder https://
    std::string coverBase = std::string(defaultCoverBase(true));   // [covers] base (M11): Cover-Quelle, http:// oder https://
    std::string retroCoverBase = std::string(defaultRetroCoverBase(true));  // [covers] retro_base (M22), leer = aus
    std::string cheatSource = std::string(defaultCheatSource(true));  // [cheats] source (M15): {id6}-Vorlage, leer = kein Laden
    ParentalConfig parental;                 // [parental] (M17, Spec v2 10.5): PIN als Hash, nie im Klartext
    std::optional<u8> wiiIos;                // [wii] ios (M13, Spec 6.2): auto → nullopt, sonst Slot 200–255
    std::string deviceName;  // [general] device_name (M26): Gerätename in Spielstand-Paketen, leer = "Wii"
    std::string emuNandPath;                 // [emunand] path (M19): z. B. "usb:/nands/emu1", leer = aus
    CloudProviderId cloudProvider = CloudProviderId::None;  // [cloud] provider (M27)
    std::string driveClientId, driveClientSecret;  // [cloud] drive_client_id/_secret: nur lesen, nie schreiben oder loggen
    std::vector<std::string> badKeys;        // ungültige Werte außerhalb von [library]/[paths] (NL_CFG_BAD), z. B. "wii.ios"
    std::string debugAutostart;  // [debug] autostart: Spielschlüssel, der nach NL_READY startet
    std::string debugCoverUrl;   // [debug] cover_url: ersetzt die GameTDB-Basis (Smoke-Test)
    std::string debugCheatUrl;   // [debug] cheat_url: ersetzt [cheats] source (Smoke, M15)
    std::string debugCheatFetch; // [debug] cheat_fetch = <ID6>: Codes nach NL_READY laden (Smoke, M15)
    bool debugFetchCovers = false;  // [debug] fetch_covers = 1: „Fehlende Cover laden“ nach NL_READY
    int debugClearCovers = 0;  // [debug] clear_covers (M24): 1 = nach NL_COVER_DL_DONE, 2 = gleich nach dem Download-Start
    bool debugSysinfoSave = false;  // [debug] sysinfo_save = 1: <data>/sysinfo.txt nach NL_READY schreiben (Smoke, M13)
    bool debugPerf = false;         // [debug] perf = 1: NL_PERF-Zeilen für die Hardware-Messung (M7)
    bool debugRemote = false;       // [debug] remote = 1: Fernsteuerung auf TCP 4300 (M20), sonst nur mit argv --remote
    std::string debugUpdateUrl;     // [debug] update_url: ersetzt die Manifest-URL des Selbst-Updates (Smoke, M20)
    bool debugSelfUpdate = false;   // [debug] self_update = 1: „Update installieren“ nach NL_READY (Smoke, M20)
    bool debugIgnoreAhbprot = false;  // [debug] ignore_ahbprot = 1: AHBPROT-Prüfung für GC überbrücken (Dolphin-Smoke, M8)
    std::string debugTlsCa;         // [debug] tls_ca: PEM der Test-CA statt Mozilla-Bundle (Smoke, M11)
    std::string debugOptionSet;     // [debug] option_set = <schlüssel>=<wert>: in der Options-Szene setzen und schließen (M14)
    std::string debugBannerFile;    // [debug] banner_file: Kanal-content0 statt NAND für die Detailansicht (Smoke, M18)
    int debugDetailsBackMs = 0;     // [debug] details_back_ms: so lange nach open_scene=details ein „Zurück“ einspeisen (Smoke, M18)
    std::string debugOpenScene;     // [debug] open_scene = about: Szene nach NL_READY öffnen (Smoke, M11)
    bool debugWiiDryRun = false;    // [debug] wii_dry_run = 1: Wii-Spiel nur prüfen (NL_WII_DRYRUN), nicht starten (M9)
    bool debugLogSounds = false;    // [debug] log_sounds = 1: NL_SOUND <name> je gespieltem Klang (Smoke-Test)
    bool debugSdAsDisk = false;     // [debug] sd_as_disk = 1: SD-Partitionen wie eine USB-Platte einhängen, nur lesend (Smoke, M16)
    bool debugListGames = false;    // [debug] list_games = 1: NL_GAME <schlüssel> <gerät> je Spiel nach dem Laden (Smoke, M12)
    std::string debugEmuRepo;       // [debug] emu_repo: ersetzt emulatorRepo (Smoke-Test)
    std::string debugEmuInstall;    // [debug] emu_install: Komma-Liste, nach NL_READY installieren (Smoke-Test)
    std::string debugFilter;        // [debug] filter = <spec>: Filter der Startleiste setzen, NL_FILTER loggen (Smoke, M17)
    std::string debugPinInput;      // [debug] pin_input = <4 Ziffern>: PIN-Abfrage ohne Szene beantworten (Smoke, M17)
    std::string debugWizard;        // [debug] wizard = accept: Einrichtungs-Assistent nimmt jeden Vorschlag an (Smoke, M13)
    std::string debugFakeDisc;      // [debug] fake_disc = <ID6>|gc:<ID6>: eingelegte Disc vortäuschen (Smoke, M19)
    std::string debugSaveBackup;    // [debug] save_backup = <spielschlüssel>: Spielstand nach NL_READY sichern (Smoke, M26)
    std::string debugSaveRestore;   // [debug] save_restore = <spielschlüssel>|latest: neueste Sicherung zurückspielen (M26)
    std::string debugCloudBase;     // [debug] cloud_base: Fake-Server statt Google (OAuth und API, Smoke, M27)
    bool debugCloudDeviceAuto = false;  // [debug] cloud_device_auto = 1: nach NL_READY anmelden (Smoke, M27)
    std::string debugCloudConflict;  // [debug] cloud_conflict = wii|cloud|cancel: Konfliktdialog beantworten (M27)
    std::string debugCloudSync;     // [debug] cloud_sync = <spielschlüssel>: Start-Tor ohne Start durchlaufen (M27)
};

// tls: Build mit HTTPS (NL_TLS). Bestimmt die Standardquellen; die offiziellen Quellen in http- oder https-Form
// folgen dem Build (M10 schrieb die HTTP-Form aus), fremde Quellen bleiben unverändert.
Config configFromIni(const IniDoc& ini, bool tls = true);
// Liest nur [debug] in c (auch aus <data>/debug.ini, wenn config.ini noch fehlt; M13 Task 5).
void applyDebugIni(Config& c, const IniDoc& ini);
std::string defaultConfigText(bool tls = true);
int parseClock(std::string_view hhmm, int fallback);  // "19:00" → 1140
std::string formatClock(int minuteOfDay);              // 1140 → "19:00"

// Schreibt alle Schlüssel aus Spec 5.4 in den bisherigen Dateitext. Kommentare, unbekannte Schlüssel,
// fremde Abschnitte (auch [debug]) und das Zeilenende (CRLF/LF) bleiben erhalten; fehlende Schlüssel
// kommen ans Ende ihres Abschnitts, fehlende Abschnitte ans Dateiende.
std::string configToIniText(const Config& c, std::string_view previousText);

}  // namespace nl
