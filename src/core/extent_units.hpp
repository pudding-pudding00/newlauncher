#pragma once
// Extents aus den Läufen einer Dateisystem-Bibliothek (M16, Spec v2 9.2): Läufe in Bibliothekseinheiten
// (NTFS: Cluster, relativ zur Partition) → Extents in 512-Byte-Sektoren, absolut auf dem Gerät.

#include "core/fat_chain.hpp"
#include "core/result.hpp"
#include "core/types.hpp"

#include <span>
#include <vector>

namespace nl {

struct FsRun {
    u64 fileUnit;  // Offset in der Datei, in Einheiten
    u64 devUnit;   // Lage in der Partition, in Einheiten
    u64 units;
    bool hole;     // sparse oder nicht initialisiert: liegt nicht auf dem Gerät
};

enum class ExtentError : u8 { Unsupported, Overflow, Gap };

// partLba in Gerätesektoren (deviceSectorBytes: 512 oder 4096); Ergebnis endet bei ceil(fileSize / 512).
// Loch im Dateibereich oder Einheit, die kein Vielfaches von 512 ist → Unsupported; fehlender Bereich → Gap;
// Sektor ab 2^32 → Overflow. Angrenzende Läufe werden verschmolzen.
Result<std::vector<Extent>, ExtentError> runsToExtents(std::span<const FsRun> runs, u32 unitBytes, u64 partLba,
                                                       u32 deviceSectorBytes, u64 fileSize);

}  // namespace nl
