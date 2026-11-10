#pragma once
// Preflight für den Emulator-Start (Spec 9, M3): prüft alles, was vor dem Chainload schiefgehen kann,
// solange der Launcher noch Fehler anzeigen kann. Schreibt nichts.

#include <string>
#include <string_view>
#include <vector>

#include "core/emulators.hpp"
#include "core/fs.hpp"
#include "core/game.hpp"
#include "core/result.hpp"

namespace nl {

// Warum ein Start scheitert; bestimmt den Dialogtext (nicht die Dateiendung).
enum class LaunchFailure : u8 { Unsupported, EmuMissing, EmuInvalid, RomMissing, ArgsTooLong, NoMemory };
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

// Fehler: Unsupported (Konsole ohne Backend, Kontext Konsolenname), NotFound (DOL oder ROM fehlt,
// Kontext Pfad), InvalidData (DOL ungültig/zu groß mit DOL-Pfad, oder argv zu lang).
Result<LaunchPlan, LaunchError> preflightEmulator(FileSystem& fs, const Game& g, std::string_view libRoot, const EmulatorTable& emus);

}  // namespace nl
