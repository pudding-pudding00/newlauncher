#pragma once
// Fragmentliste für den Game-Booter (M9 Task 3): Disc-Sektor → Gerätesektor, im Speicherformat von
// WiiFlow Lites FragList (frag.h). Alle Angaben in 512-Byte-Sektoren.

#include "core/fat_chain.hpp"
#include "core/wbfs_map.hpp"

namespace nl {

inline constexpr u32 kMaxFrag = 20000;           // MAX_FRAG in booter_cfg.h
inline constexpr u32 kFragListBytes = 12 + kMaxFrag * 12;

struct FilePart {
    u64 size;                     // Dateigröße in Byte
    std::vector<Extent> extents;  // aus fileExtents, clustergerade
};

struct FragEntry {
    u32 offset;  // Disc-Sektor
    u32 sector;  // Gerätesektor (LBA)
    u32 count;
};

struct FragList {
    u32 size = 0;  // Ende des letzten Fragments in Sektoren (frag.c:64)
    std::vector<FragEntry> entries;
};

// Eine ISO bildet sich 1:1 ab; size = ceil(Dateigröße / 512).
Result<FragList, FragError> buildIsoFragList(const FilePart& iso);

// Split-Dateien (.wbfs, .wbf1 …): Teil i beginnt bei i × parts[0].size, sobald es mehr als einen Teil gibt.
Result<FragList, FragError> buildWbfsFragList(const WbfsLayout& layout, std::span<const FilePart> parts);

// Genau kFragListBytes Byte in Big-Endian: size, num, maxnum, dann die Einträge; Rest 0.
std::vector<u8> serializeFragList(const FragList& list);

// CRC-32 (IEEE) über die serialisierte Liste, für den Marker neben der Liste.
u32 fragListCrc(std::span<const u8> bytes);

}  // namespace nl
