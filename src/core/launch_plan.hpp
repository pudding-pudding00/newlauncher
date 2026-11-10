#pragma once
// Preflight für den Start (Spec 9, M3/M4): prüft alles, was vor dem Chainload bzw. Kanalstart
// schiefgehen kann, solange der Launcher noch Fehler anzeigen kann. Schreibt nichts.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/emulators.hpp"
#include "core/fs.hpp"
#include "core/game.hpp"
#include "core/n64_choice.hpp"
#include "core/result.hpp"

namespace nl {

// Warum ein Start scheitert; bestimmt den Dialogtext (nicht die Dateiendung).
enum class LaunchFailure : u8 { Unsupported, EmuMissing, EmuInvalid, RomMissing, ArgsTooLong, NoMemory, NoN64Path };
const char* launchFailureName(LaunchFailure f);  // Log-Marker NL_PREFLIGHT_FAIL

struct LaunchError {
    LaunchFailure kind;
    std::string context;  // Konsolenname bzw. Pfad
};

struct LaunchPlan {
    std::string dolPath;
    u32 dolSize = 0;
    std::vector<std::string> args;
    std::vector<u8> argvBlock;  // für kArgsAddr gepackt
};

// Größe einer Datei über den Ordnereintrag; nullopt, wenn Datei oder Ordner fehlt.
std::optional<u64> fileSize(FileSystem& fs, std::string_view path);

// Fehler: Unsupported (Konsole ohne Backend, Kontext Konsolenname), EmuMissing/EmuInvalid (Kontext
// DOL-Pfad), RomMissing (Kontext ROM-Pfad), ArgsTooLong.
Result<LaunchPlan, LaunchError> preflightEmulator(FileSystem& fs, const Game& g, std::string_view libRoot, const EmulatorTable& emus);

// Ziel eines Starts: ein DOL über den Chainloader oder ein Kanal im NAND (VC-Inject).
struct LaunchTarget {
    enum class Kind : u8 { Dol, Channel } kind = Kind::Dol;
    LaunchPlan dol;                // Kind::Dol
    u64 titleId = 0;               // Kind::Channel
    std::optional<N64Method> n64;  // gewählter Weg bei N64-Spielen
};

// Wie preflightEmulator, N64 zusätzlich über die Auto-Wahl (n64 == nullptr zählt als „kein Weg“).
// Kein Weg → NoN64Path mit Konsolenname; VC → Kanalziel ohne Dateizugriff; Not64/Wii64 → DOL-Prüfung.
Result<LaunchTarget, LaunchError> preflightGame(FileSystem& fs, const Game& g, std::string_view libRoot, const EmulatorTable& emus,
                                                const N64Inputs* n64);

}  // namespace nl
