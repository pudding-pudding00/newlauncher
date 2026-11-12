#pragma once
// FAT32 über Rohsektoren (M9 Task 2): Volume finden und die Clusterkette einer Datei als
// zusammenhängende Sektorbereiche (Extents) liefern. Nur 512-Byte-Sektoren.

#include "core/block_dev.hpp"
#include "core/result.hpp"
#include "core/types.hpp"

#include <vector>

namespace nl {

enum class FatError : u8 { Io, SectorSize, NoFatVolume, NotFat32, BadChain, ChainTooShort };

struct FatVolume {
    u32 partLba = 0;            // erster Sektor des Volumes (Bootsektor)
    u32 fatLba = 0;             // erste FAT
    u32 dataLba = 0;            // Cluster 2
    u32 sectorsPerCluster = 0;
    u32 clusterCount = 0;       // gültige Cluster: 2 … clusterCount + 1
};

// Sektor 0 als Bootsektor, sonst die vier primären MBR-Einträge in Reihenfolge; der erste gültige gilt.
Result<FatVolume, FatError> findFatVolume(BlockDevice& dev);
// Bootsektor genau an `partLba` (eine von mehreren eingehängten Partitionen, M12); 0 sucht wie findFatVolume.
Result<FatVolume, FatError> findFatVolumeAt(BlockDevice& dev, u32 partLba);

struct Extent {
    u32 fileSector;  // Offset in der Datei, in Sektoren
    u32 lba;         // absolut auf dem Gerät
    u32 count;       // Sektoren
    bool operator==(const Extent&) const = default;
};

// Folgt der Kette ab `startCluster`, bis ceil(fileSize / Clustergröße) Cluster beisammen sind; lückenlose
// Cluster werden verschmolzen. Danach muss die Kette enden (sonst BadChain), damit eine Schleife oder eine
// fremde Kette nie still als Datei durchgeht. Höchstens Clusterzahl + 1 FAT-Einträge werden gelesen.
Result<std::vector<Extent>, FatError> fileExtents(BlockDevice& dev, const FatVolume& vol, u32 startCluster, u64 fileSize);

}  // namespace nl
