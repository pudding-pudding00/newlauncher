#pragma once
// FST eines GC-Abbilds (M18): Disc-Kopf 0x424 = FST-Offset, 0x428 = FST-Größe. Einträge zu 12 Byte:
// Wort 0 = Ordner-Flag (Bit 24) | Namensoffset (24 Bit), Wort 1 = Dateioffset bzw. Elternindex,
// Wort 2 = Dateigröße bzw. Index nach dem letzten Kind. Eintrag 0 ist die Wurzel, Wort 2 = Anzahl der Einträge,
// dahinter die Namenstabelle. Formatbeschreibung: WiiBrew „GameCube“/„Wii Disc“ (FST). Wii-Abbilder speichern
// Dateioffsets ×4; der Aufrufer rechnet um.
// Das FST kommt vom Spielgerät und gilt als nicht vertrauenswürdig: jeder Zugriff ist grenzgeprüft.

#include <span>
#include <string_view>

#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

struct FstFile {
    u32 offset;
    u32 size;
};

// Sucht nur im Wurzelverzeichnis, Groß/klein egal. Fehlt die Datei → NotFound; kaputtes FST → InvalidData.
Result<FstFile> findRootFile(std::span<const u8> fst, std::string_view name);

}  // namespace nl
