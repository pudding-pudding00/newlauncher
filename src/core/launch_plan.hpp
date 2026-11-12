#pragma once
// Preflight für den Start (Spec 9, M3/M4): prüft alles, was vor dem Chainload bzw. Kanalstart
// schiefgehen kann, solange der Launcher noch Fehler anzeigen kann. Schreibt nichts.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/emulators.hpp"
#include "core/fat_chain.hpp"
#include "core/frag_list.hpp"
#include "core/ios_info.hpp"
#include "core/fs.hpp"
#include "core/game.hpp"
#include "core/n64_choice.hpp"
#include "core/nin_cfg.hpp"
#include "core/result.hpp"

namespace nl {

// Warum ein Start scheitert; bestimmt den Dialogtext (nicht die Dateiendung).
enum class LaunchFailure : u8 {
    Unsupported, EmuMissing, EmuInvalid, RomMissing, ArgsTooLong, NoMemory, NoN64Path,
    NinMissing, NinUnknown, NinTooOld, NinPathTooLong, AhbProt, NinFormat,  // M8/M12: GameCube über Nintendont
    CiosMissing, CiosNotD2x, WiiFsUnsupported, WiiFileBroken,     // M9: Wii-Spiele nativ
    DeviceUnsupported,  // M12: GC/ROM auf usb2…usb8 – Nintendont und GX-Emulatoren sehen nur sd: und usb:
};
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

// M9: Fehler beim Abbilden einer Wii-Spieldatei. SectorSize/NoFatVolume/NotFat32 → WiiFsUnsupported,
// PartMissing → RomMissing (Kontext: Pfad des fehlenden Teils), alles andere → WiiFileBroken.
LaunchError fromFatError(FatError e, std::string context);
LaunchError fromFragError(FragError e, std::string context);

// Alles, was der Game-Booter braucht; the_CFG entsteht daraus erst auf der Plattform (Task 6).
struct WiiBootPlan {
    std::string id6;
    bool usb = false;
    CiosStatus cios;
    std::vector<u8> fragBytes;  // serializeFragList, genau kFragListBytes
};

// Reihenfolge: cIOS Missing → CiosMissing; NotD2x → CiosNotD2x (Kontext Name); beides bei dryRun nur
// durchgelassen (die Plattform loggt es); dann Fragmentfehler durchreichen; Schlüssel ohne ID6 →
// Unsupported; sonst Plan. usb folgt aus libRoot („usb:“).
Result<WiiBootPlan, LaunchError> preflightWii(const Game& g, std::string_view libRoot, const CiosStatus& cios,
                                              const Result<FragList, LaunchError>& frags, bool dryRun);

// Ziel eines Starts: ein DOL über den Chainloader, ein Kanal im NAND (VC-Inject) oder ein Wii-Spiel.
struct LaunchTarget {
    enum class Kind : u8 { Dol, Channel, Wii } kind = Kind::Dol;
    LaunchPlan dol;                // Kind::Dol
    u64 titleId = 0;               // Kind::Channel
    std::optional<N64Method> n64;  // gewählter Weg bei N64-Spielen
    std::optional<NinVersion> nintendont;  // GC: erkannte Version (Marker NL_NIN)
    std::optional<WiiBootPlan> wii;        // Kind::Wii: erst nach preflightWii gesetzt
};

// Wie preflightEmulator, N64 zusätzlich über die Auto-Wahl (n64 == nullptr zählt als „kein Weg“).
// Kein Weg → NoN64Path mit Konsolenname; VC → Kanalziel ohne Dateizugriff; Not64/Wii64 → DOL-Prüfung.
// GC (Plan M8), in dieser Reihenfolge: !ahbprotOff → AhbProt; DOL fehlt → NinMissing; DOL ungültig →
// EmuInvalid; keine Version → NinUnknown; zu alt → NinTooOld (Kontext gefundene Version); Spiel fehlt →
// RomMissing; Pfad > 254 → NinPathTooLong; sonst args = {DOL, NIN_CFG}. Andere Konsolen ignorieren ahbprotOff.
// Wii (M9): immer Kind::Wii ohne Plan; die Plattform rechnet die Fragmente und ruft preflightWii.
Result<LaunchTarget, LaunchError> preflightGame(FileSystem& fs, const Game& g, std::string_view libRoot, const EmulatorTable& emus,
                                                const N64Inputs* n64, bool ahbprotOff);

}  // namespace nl
