#pragma once
// Partitionstabellen (MBR, EBR-Kette, GPT) und Dateisystemerkennung für SD und USB (Spec v2 1, M12).

#include "core/block_dev.hpp"
#include "core/result.hpp"
#include "core/types.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace nl {

enum class FsKind : u8 { Fat, ExFat, Ntfs, Ext, Wbfs, Unknown };
inline constexpr u8 fsBit(FsKind k) { return static_cast<u8>(1u << static_cast<unsigned>(k)); }  // M16: fsMask

struct PartitionInfo {
    u64 lba;       // erster Sektor, in Gerätesektoren
    u64 sectors;
    u8 mbrType;    // Typbyte aus MBR/EBR; 0xEE für GPT-Einträge, 0 für ein Superfloppy-Abbild
    FsKind kind;   // aus dem Bootsektor bzw. Superblock, nicht aus dem Typbyte
    bool logical;  // aus einer EBR-Kette
    bool beyondLimit = false;  // endet hinter Sektor 2^32: nur fürs Log, wird nicht eingehängt
};

inline constexpr std::size_t kMaxPartitions = 16;

// Erkennt das Dateisystem am Anfang einer Partition. ext wird nur erkannt, wenn `firstSector` mindestens
// die ersten 1082 Byte umfasst (Superblock bei Byte 1024, Magic an Offset 56).
FsKind detectFs(std::span<const u8> firstSector);

// Liest die Partitionen eines Geräts in Tabellenreihenfolge (primäre vor logischen), höchstens kMaxPartitions.
// Partitionen mit Länge 0, jenseits von `deviceSectors` oder jenseits der 32-Bit-LBA von BlockDevice fehlen.
// deviceSectors = 0 heißt unbekannt (libogc nennt die Größe nicht): dann keine Prüfung gegen das Plattenende.
// IoError, wenn ein Tabellensektor nicht lesbar ist; InvalidData bei fehlender Signatur oder falscher GPT-CRC.
Result<std::vector<PartitionInfo>> readPartitions(BlockDevice& dev, u64 deviceSectors);

}  // namespace nl
