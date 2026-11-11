#pragma once
// Emulator-Verwaltung (Spec 6.9): fester Katalog, Open-Shop-Channel-Liste (listv036.txt), installierter Stand
// (emulators_installed.ini) und der daraus berechnete Status je Emulator. Plattformfrei, host-getestet.

#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.hpp"

namespace nl {

// Open Shop Channel: HTTPS mit TLS-Build, sonst HTTP (M11 Task 8: Liste über beide Schemata identisch, 2026-11-11).
inline constexpr std::string_view kDefaultEmuRepoHttps = "https://hbb1.oscwii.org/hbb";
inline constexpr std::string_view kDefaultEmuRepoHttp = "http://hbb1.oscwii.org/hbb";
constexpr std::string_view defaultEmuRepo(bool tls) { return tls ? kDefaultEmuRepoHttps : kDefaultEmuRepoHttp; }
inline constexpr std::string_view kInstalledEmusPath = "sd:/newlauncher/emulators_installed.ini";

struct OscApp {
    std::string name;  // wie in der Liste, auch Groß-/Kleinschreibung (Teil der ZIP-URL)
    u32 timestamp = 0;
    u32 unzippedSize = 0;
    u32 zipSize = 0;
    std::string displayName, version;
};

// Pro App sieben Zeilen; die erste: Name, Zeitstempel, Icon-Größe, entpackte Größe, Typ, ZIP-Größe, …
// Kopfzeile, Kategorien (=Name=), Blöcke mit kaputten Zahlen und ein abgeschnittener letzter Block werden
// übersprungen. CRLF und LF.
std::vector<OscApp> parseOscList(std::string_view text);

struct EmuInfo {
    std::string_view oscName;
    std::string_view label;  // Anzeigename im Launcher
    std::string_view dol;
};
std::span<const EmuInfo> emulatorCatalog();  // Reihenfolge wie Spec 6.9

struct InstalledEmu {
    u32 timestamp = 0;
    std::string version;
};
// Nur Katalog-Namen; Abschnitte mit ungültigem timestamp fehlen im Ergebnis.
std::map<std::string, InstalledEmu> parseInstalledEmus(std::string_view ini);
std::string formatInstalledEmus(const std::map<std::string, InstalledEmu>& m);

enum class EmuState : u8 { NotInstalled, Current, UpdateAvailable, UnknownVersion };

struct EmuRow {
    const EmuInfo* info = nullptr;
    EmuState state = EmuState::NotInstalled;
    std::optional<InstalledEmu> installed;  // Eintrag aus emulators_installed.ini (nur wenn die DOL existiert)
    std::optional<OscApp> remote;           // leer: Liste nicht geladen oder App fehlt darin
};

// DOL fehlt → NotInstalled; DOL da ohne Eintrag → UnknownVersion; Eintrag älter als die Liste → UpdateAvailable;
// sonst Current.
std::vector<EmuRow> emuRows(const std::map<std::string, InstalledEmu>& installed,
                            const std::function<bool(std::string_view dol)>& dolExists, std::span<const OscApp> list);

std::string oscListUrl(std::string_view repo);
std::string oscZipUrl(std::string_view repo, std::string_view name);

}  // namespace nl
