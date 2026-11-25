#pragma once
// Selbst-Update von newlauncher (M20): Manifest update.ini aus dem GitHub-Release, Versionsvergleich und die
// Beschreibung für den ZIP-Installer (installZipApp). Plattformfrei.
//
// update.ini:
//   [update]
//   version = 0.2.0
//   zip = newlauncher-0.2.0.zip        ; Dateiname neben update.ini oder absolute https-URL
//   zip_size = 123456
//   unzipped_size = 400000

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "core/emu_catalog.hpp"
#include "core/emu_install.hpp"
#include "core/fs.hpp"
#include "core/types.hpp"

namespace nl {

inline constexpr std::string_view kDefaultUpdateUrl =
    "https://github.com/pudding-pudding00/newlauncher/releases/latest/download/update.ini";
inline constexpr u32 kMaxUpdateZipBytes = 32u * 1024 * 1024;

struct UpdateManifest {
    std::string version;
    std::string zipUrl;  // immer https
    u32 zipSize = 0;
    u32 unzippedSize = 0;
};

// true, wenn candidate (a.b.c, nur Ziffern) echt neuer ist als current; fehlende Teile zählen als 0.
// Eine kaputte Angabe ist nie neuer.
bool versionNewer(std::string_view candidate, std::string_view current);

// nullopt bei fehlenden/kaputten Feldern, zip_size 0 oder > kMaxUpdateZipBytes, http statt https (auch bei der
// Manifest-URL) oder einem Dateinamen mit Pfadanteil.
std::optional<UpdateManifest> parseUpdateManifest(std::string_view text, std::string_view manifestUrl);

// Eintrag für installZipApp: Name "newlauncher" → apps/newlauncher/boot.dol muss im ZIP liegen.
OscApp selfUpdateApp(const UpdateManifest& m);

enum class SelfUpdateOutcome : u8 { UpToDate, Installed, NoManifest, BadManifest, Failed };

struct SelfUpdateResult {
    SelfUpdateOutcome outcome = SelfUpdateOutcome::Failed;
    std::string version;       // Version im Manifest (leer ohne Manifest)
    EmuInstallResult install;  // bei Failed
};

// Text des Manifests oder nullopt (Netzfehler, 404, zu groß).
using ManifestFetch = std::function<std::optional<std::string>(const std::string& url)>;

// Manifest laden, mit currentVersion vergleichen und nur bei echt neuerer Version installZipApp aufrufen.
SelfUpdateResult runSelfUpdate(FileSystem& fs, EmuInstallIo& io, const ManifestFetch& fetch,
                               std::string_view manifestUrl, std::string_view currentVersion,
                               std::string_view root = "sd:");

}  // namespace nl
