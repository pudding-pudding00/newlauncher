#pragma once
// Preflight für den Start (Spec 9, M3/M4): prüft alles, was vor dem Chainload bzw. Kanalstart
// schiefgehen kann, solange der Launcher noch Fehler anzeigen kann. Schreibt nichts.

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/disc_id.hpp"
#include "core/emulators.hpp"
#include "core/partition_table.hpp"
#include "core/extent_units.hpp"
#include "core/fat_chain.hpp"
#include "core/frag_list.hpp"
#include "core/ios_info.hpp"
#include "core/fs.hpp"
#include "core/game.hpp"
#include "core/game_options.hpp"
#include "core/n64_choice.hpp"
#include "core/nin_cfg.hpp"
#include "core/result.hpp"
#include "core/sysinfo.hpp"
#include "core/wip_patch.hpp"

namespace nl {

// Warum ein Start scheitert; bestimmt den Dialogtext (nicht die Dateiendung).
enum class LaunchFailure : u8 {
    Unsupported, EmuMissing, EmuInvalid, RomMissing, ArgsTooLong, NoMemory, NoN64Path,
    NinMissing, NinUnknown, NinTooOld, NinPathTooLong, AhbProt, NinFormat,  // M8/M12: GameCube über Nintendont
    CiosMissing, WiiFsUnsupported, WiiFileBroken,                 // M9: Wii-Spiele nativ; CiosMissing: Kontext Slot
    DeviceUnsupported,  // M12: GC/ROM auf usb2…usb8 – Nintendont und GX-Emulatoren sehen nur sd: und usb:
    CheatsTooLarge,     // M15: GCT passt nicht in die Codeliste; Kontext Größe in Bytes
    FileNotContiguousReadable,  // M16: NTFS-Datei komprimiert, sparse oder resident; Kontext Pfad
    NintendontNeedsFat,         // M16: GC-Spiel nicht auf FAT; Kontext Gerätename („USB 2“)
    DiscChanged,                // M19: eingelegte Disc weicht vor dem Start ab; Kontext neue ID6
    DiscUnreadable,             // M19: eingelegte Disc beim Start leer oder unlesbar
    GcDiscUnsupported,          // M19: GC-Disc auf vWii oder Konsole ohne GC-Ports
};
const char* launchFailureName(LaunchFailure f);  // Log-Marker NL_PREFLIGHT_FAIL

// M14: Hinweise, die den Start nicht verhindern (Spec 7.3).
enum class LaunchWarning : u8 {
    MemcardRealNoPorts, NativeSiNoPorts,
    EmulatorFs,  // M16: ROM auf einem Dateisystem außerhalb von fsMask; Name in LaunchTarget::emuName
};
const char* launchWarningName(LaunchWarning w);  // Log-Marker NL_PREFLIGHT_WARN

// M14: Quellen der Spieloptionen; gcPorts = false auf vWii/Wii mini.
struct GameOptionInputs {
    const IniDoc& config;
    const IniDoc& games;
    bool gcPorts = true;
    std::span<const u8> gcCheats = {};  // M15: GCT aus .txt/.sel; wirkt nur mit GC-Option „cheats“
    Platform platform = Platform::Wii;  // M19: Wii vs. vWii (GC-Disc, Standard wiiu_wide)
};

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
LaunchError fromExtentError(ExtentError e, std::string context);  // M16: Extents aus NTFS

// M15: Platz für die GCT hinter dem Codehandler, 0x80002338 (Symbol codelist des gebauten Handlers) bis
// 0x80003000 (Beginn der OS-Globals, wie upstream WiiFlow). Der Wii-Build prüft beide Grenzen gegen den Handler.
inline constexpr u32 kWiiCodelistBytes = 0x80003000u - 0x80002338u;

enum class WiiBootSource : u8 { Usb, Disc, Channel };

// Alles, was der Game-Booter braucht; the_CFG entsteht daraus erst auf der Plattform (Task 6).
struct WiiBootPlan {
    WiiBootSource source = WiiBootSource::Usb;
    std::string id6;
    bool usb = false;
    CiosStatus cios;
    u8 iosSlot = kDefaultCiosSlot;  // M13: IOS_ReloadIOS und the_CFG.IOS.Version (= cios.slot)
    std::vector<u8> fragBytes;  // serializeFragList, genau kFragListBytes (nur Usb)
    WiiOptions options;         // M14: the_CFG-Felder, ios und Rückkehr
    std::vector<u8> gct;        // M15: gewählte Codes als GCT; leer = ohne Cheats
    u8 hookType = 0;            // M15: the_CFG.hooktype, 0 ohne Codes
    std::vector<WipEntry> wip;  // M15: WIP-Patches aus <ID6>.wip, höchstens kWipMaxEntries
};

// Reihenfolge: cIOS Missing → CiosMissing (Kontext Slot, z. B. "222"); bei dryRun nur durchgelassen (die
// Plattform loggt es); dann Fragmentfehler durchreichen; Schlüssel ohne ID6 → Unsupported; GCT größer als
// kWiiCodelistBytes → CheatsTooLarge (Kontext Größe in Bytes, auch im Dry-Run); sonst Plan.
// cios kommt aus ciosStatusFor (core/cios_scan); usb folgt aus libRoot („usb:“).
Result<WiiBootPlan, LaunchError> preflightWii(const Game& g, std::string_view libRoot, const CiosStatus& cios,
                                              const Result<FragList, LaunchError>& frags, bool dryRun,
                                              const WiiOptions& options = {}, std::span<const u8> gct = {},
                                              std::span<const WipEntry> wip = {});

// M19: Wii-Disc vor dem Start gegen das tatsächlich eingelegte Medium prüfen (TYPE_WII_DISC, ohne Fragmente).
Result<WiiBootPlan, LaunchError> preflightWiiDisc(const Game& g, std::optional<OpticalDisc> currentDisc,
                                                  const CiosStatus& cios, bool dryRun,
                                                  const WiiOptions& options = {}, std::span<const u8> gct = {},
                                                  std::span<const WipEntry> wip = {});

// Ziel eines Starts: ein DOL über den Chainloader, ein Kanal im NAND (VC-Inject) oder ein Wii-Spiel.
struct LaunchTarget {
    enum class Kind : u8 { Dol, Channel, Wii } kind = Kind::Dol;
    LaunchPlan dol;                // Kind::Dol
    u64 titleId = 0;               // Kind::Channel
    std::optional<N64Method> n64;  // gewählter Weg bei N64-Spielen
    std::optional<NinVersion> nintendont;  // GC: erkannte Version (Marker NL_NIN)
    std::optional<WiiBootPlan> wii;        // Kind::Wii: erst nach preflightWii gesetzt
    std::optional<GcOptions> gc;           // GC mit opts: aufgelöste Optionen (Marker NL_NIN_CFG, NL_OPT_*)
    std::vector<LaunchWarning> warnings;   // M14: Hinweise, Start trotzdem möglich
    std::vector<u8> gct;                   // M15 GC: vor dem Start nach gctPath schreiben (Nintendont CheatPath)
    std::string gctPath;                   // <spielgerät>:/newlauncher/cache/<ID6>.gct
    std::string emuName;                   // M16: Emulator für den Hinweis EmulatorFs
};

// Wie preflightEmulator, N64 zusätzlich über die Auto-Wahl (n64 == nullptr zählt als „kein Weg“).
// Kein Weg → NoN64Path mit Konsolenname; VC → Kanalziel ohne Dateizugriff; Not64/Wii64 → DOL-Prüfung.
// GC (Plan M8), in dieser Reihenfolge: !ahbprotOff → AhbProt; DOL fehlt → NinMissing; DOL ungültig →
// EmuInvalid; keine Version → NinUnknown; zu alt → NinTooOld (Kontext gefundene Version); Spiel fehlt →
// RomMissing; Pfad > 254 → NinPathTooLong; sonst args = {DOL, NIN_CFG}. Andere Konsolen ignorieren ahbprotOff.
// Wii (M9): immer Kind::Wii ohne Plan; die Plattform rechnet die Fragmente und ruft preflightWii.
// M16, deviceFs = Dateisystem des Spielgeräts: GC nicht auf FAT → NintendontNeedsFat (vor allen anderen
// GC-Prüfungen); ROM/N64-DOL außerhalb von emulatorFsMask → Hinweis EmulatorFs; Wii: jedes Dateisystem.
Result<LaunchTarget, LaunchError> preflightGame(FileSystem& fs, const Game& g, std::string_view libRoot, const EmulatorTable& emus,
                                                const N64Inputs* n64, bool ahbprotOff,
                                                const GameOptionInputs* opts = nullptr, FsKind deviceFs = FsKind::Fat);

}  // namespace nl
