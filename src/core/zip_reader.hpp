#pragma once
// ZIP-Leser für Emulator-Pakete (Spec 6.9): zentrales Verzeichnis lesen, Einträge einzeln entpacken und gegen
// Größe und CRC32 prüfen. Nur Methoden 0 (gespeichert) und 8 (Deflate), kein ZIP64, keine Verschlüsselung.

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/fs.hpp"
#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

// Größte Datei in den Paketen ist die DOL (höchstens kMaxDolBytes = 11 MiB); 16 MiB lassen Luft und passen
// neben Cover-Cache und Launcher in den Heap.
inline constexpr u32 kMaxZipEntryBytes = 16u * 1024 * 1024;
inline constexpr u32 kMaxZipEntries = 4096;

struct ZipEntry {
    std::string name;
    u16 method = 0;
    u32 crc = 0;
    u32 compSize = 0;
    u32 size = 0;
    u32 localOffset = 0;
    bool isDir = false;  // Name endet auf '/'
};

// Relativ, nicht leer, Trenner nur '/', keine leeren, "." oder ".." Segmente (ein abschließendes '/' für
// Ordner ist erlaubt), kein '\\', kein ':' und kein Steuerzeichen.
bool isSafeZipPath(std::string_view name);

// Ein unsicherer Name, eine unbekannte Methode, ein Eintrag über kMaxZipEntryBytes, Daten, die über den
// Anfang des Verzeichnisses reichen würden, oder eine unmögliche Deflate-Rate machen das ganze Archiv
// ungültig (ErrorCode::InvalidData, Kontext nennt den Eintrag). So reserviert extractZipEntry nie mehr,
// als die Datei hergeben kann.
Result<std::vector<ZipEntry>> readZipDirectory(FileSystem& fs, std::string_view zip, u64 zipSize);

Result<std::vector<u8>> extractZipEntry(FileSystem& fs, std::string_view zip, const ZipEntry& e);

}  // namespace nl
