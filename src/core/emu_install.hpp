#pragma once
// Emulator-Installation (Spec 6.9, Schritte 1–5): Platz prüfen, ZIP in die Zwischenablage sd:/newlauncher/tmp
// laden, Größe und Verzeichnis prüfen, vollständig entpacken und erst dann verschieben. Dateien unter apps/
// werden ersetzt, alles andere (Einstellungen, Spielstände) nur angelegt, wenn es fehlt; boot.dol zuletzt.
// Plattformfrei: Netz, freier Platz, Abbruch und Log kommen über EmuInstallIo.

#include <functional>
#include <string>
#include <string_view>

#include "core/emu_catalog.hpp"
#include "core/fs.hpp"
#include "core/http.hpp"
#include "core/types.hpp"

namespace nl {

enum class EmuStep : u8 { Download, Verify, Extract, Move };
enum class EmuInstallError : u8 { None, NoSpace, Download, BadSize, BadZip, Write, Cancelled };

struct EmuInstallResult {
    EmuInstallError error = EmuInstallError::None;
    std::string detail;
};

struct EmuInstallIo {
    std::function<HttpOutcome(const std::string& url, const std::string& destFile, u32 maxBytes)> download;
    std::function<u64()> freeBytes;  // SD
    std::function<bool()> cancelled;  // vor dem Verschieben geprüft; das Verschieben selbst läuft durch
    std::function<void(std::string line)> log;
    std::function<void(EmuStep step, u32 done, u32 total)> progress;
};

inline constexpr std::string_view kEmuTmpDir = "sd:/newlauncher/tmp";

// Zielgerät der Installation (Spec v2 6.3): "sd:", ohne SD das <data>-Gerät (z. B. "usb:").
std::string emuTmpDir(std::string_view root);          // <root>/newlauncher/tmp
std::string installedEmusPath(std::string_view root);  // <root>/newlauncher/emulators_installed.ini
std::string onRoot(std::string_view sdPath, std::string_view root);  // "sd:/apps/x" → "<root>/apps/x"

std::string_view emuInstallErrorName(EmuInstallError e);

EmuInstallResult installEmulator(FileSystem& fs, EmuInstallIo& io, std::string_view repo, const OscApp& app,
                                 std::string_view root = "sd:");

// Löscht alle Dateien unter kEmuTmpDir (rekursiv); leere Ordner bleiben stehen.
void clearEmuTmp(FileSystem& fs, std::string_view root = "sd:");

// Beim Start vor clearEmuTmp: Fehlt die DOL eines Katalog-Emulators, liegt aber <dol>.old daneben (Strom weg
// zwischen den beiden Umbenennungen in installEmulator), kommt die alte DOL zurück. Liegen beide da, ist .old
// ein Rest und wird gelöscht.
void recoverEmuInstall(FileSystem& fs, std::string_view root = "sd:");

}  // namespace nl
