#pragma once
// Bibliotheks-Scanner (Spec 6.2): findet ROMs, GC- und Wii-Abbilder unter einem Bibliotheksgerät.
// Jeder Lauf listet alle Ordner; Disc-Header werden nur für neue oder geänderte Dateien gelesen.

#include <string_view>

#include "core/fs.hpp"
#include "core/index_file.hpp"
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

// libRoot: "sd:" oder "usb:". Pfade im Ergebnis sind relativ dazu (z. B. "roms/nes/A.nes").
Result<ScanResult> scanLibrary(FileSystem& fs, std::string_view libRoot, const IndexData* previous,
                               const MetaDb* meta, ScanProgress progress = nullptr, void* user = nullptr);

}  // namespace nl
