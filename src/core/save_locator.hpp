#pragma once
// Spielstand-Einheiten (M26, Spec Ziel 1): welche Dateien zu einem Spiel gehören und wo sie liegen.
// Rein rechnerisch über FileSystem, damit NAND (über ISFS), EmuNAND und FAT dieselbe Logik teilen.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/console.hpp"
#include "core/fs.hpp"
#include "core/result.hpp"
#include "core/save_archive.hpp"

namespace nl {

enum class SaveStore : u8 { RealNand, EmuNand, Fat };
enum class SaveKind : u8 { WiiDisc, Channel, GcSingle, GcMulti, Rom };

struct SaveQuery {
    SaveKind kind = SaveKind::WiiDisc;
    std::string id4;
    u64 titleId = 0;
    // "nand:" (echter NAND), "<dev>:<ordner>" (EmuNAND, z. B. usb:/emunand) oder "<dev>:" (Spielgerät GC).
    std::string root;
    bool jp = false;  // GC-Multikarte JP/KOR
    Console console = Console::Nes;
    std::string profile;  // ROM: erkanntes Emulator-Profil
    std::string stem;     // ROM: Dateiname ohne Endung
    std::string gameKey;  // ROM: Spielschlüssel (für die CRC in der ID)
    std::string saveDir;  // emulators.ini save_dir
    std::vector<std::string> saveExt;
};

struct SaveUnit {
    std::string id;
    SaveStore store = SaveStore::Fat;
    std::string dir;                 // absolut, ohne Ende-'/'
    std::vector<std::string> names;  // leer = ganzer Ordner rekursiv, sonst erlaubte Dateinamen
};

std::string saveUnitId(const SaveQuery& q);

// nullopt nur, wenn es keinen möglichen Ort gibt (ROM-Profil ohne bekannten Ordner); ein noch fehlender Ordner
// ist ein gültiger Ort.
std::optional<SaveUnit> locateSave(FileSystem& fs, const SaveQuery& q);

bool isSaveStateName(std::string_view name);

// Fehlender Ordner → leere Liste; Pfade relativ zu dir; Summe über maxRaw → InvalidData.
Result<std::vector<SaveFile>> readUnit(FileSystem& fs, const SaveUnit& u, std::size_t maxRaw = kMaxSaveUnitBytes);

bool unitAcceptsPath(const SaveUnit& u, std::string_view relPath);

}  // namespace nl
