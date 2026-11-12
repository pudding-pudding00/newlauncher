#pragma once
// Bibliotheks-Scanner (Spec 6.2): findet ROMs, GC- und Wii-Abbilder unter einem Bibliotheksgerät.
// Jeder Lauf listet alle Ordner; Disc-Header werden nur für neue oder geänderte Dateien gelesen.

#include <string_view>

#include "core/fs.hpp"
#include "core/index_file.hpp"
#include "core/library_sources.hpp"
#include "core/meta_db.hpp"

namespace nl {

struct ScanStats {
    u32 added = 0, removed = 0, headersRead = 0, skipped = 0;
};

struct ScanResult {
    IndexData index;
    ScanStats stats;
};

// Liefert false, um den Scan abzubrechen (z. B. beim Beenden, bevor Laufwerke ausgehängt werden).
using ScanProgress = bool (*)(void* user, u32 filesSeen);

// Ordner, Endungen und Suchtiefe je Konsole (Spec v2 5.3). zipConsoles: Bit = Console-Wert; ein .zip zählt nur
// für diese Konsolen und nur, wenn „.zip“ in ihren Endungen steht. maxDepth 1 = nur direkt im Ordner.
struct ScanRules {
    std::array<ConsolePaths, kConsoleCount> paths;
    u8 zipConsoles = 0b0101'0111;  // Nes 0, Snes 1, Gb 2, Gbc 4, Gba 6
    u8 maxDepth = 2;
};

ScanRules defaultScanRules();  // Ordner und Endungen aus der Konsolentabelle

// libRoot: "sd:" oder "usb:". Pfade im Ergebnis sind relativ dazu (z. B. "roms/nes/A.nes").
Result<ScanResult> scanLibrary(FileSystem& fs, std::string_view libRoot, const ScanRules& rules, const IndexData* previous,
                               const MetaDb* meta, ScanProgress progress = nullptr, void* user = nullptr);
// Wie oben mit defaultScanRules().
Result<ScanResult> scanLibrary(FileSystem& fs, std::string_view libRoot, const IndexData* previous,
                               const MetaDb* meta, ScanProgress progress = nullptr, void* user = nullptr);

}  // namespace nl
