#pragma once
// WBFS-Partition (M16 Task 5, Spec v2 9.2): Partitionskopf (wbfs_head_t im ersten hd-Sektor, Disc-Tabelle ab
// Byte 12), Disc-Info je Slot und die Fragmentliste eines Slots. Layout und Grenzen wie libwbfs (wbfs_open_partition).

#include "core/block_dev.hpp"
#include "core/frag_list.hpp"
#include "core/index_file.hpp"
#include "core/meta_db.hpp"
#include "core/result.hpp"
#include "core/wbfs_map.hpp"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace nl {

struct WbfsPartHead {
    u32 nHdSec;                  // Partitionsgröße in hd-Sektoren
    u8 hdSecShift;               // 9 … 12
    u8 wbfsSecShift;             // 15 … 30, ≥ hdSecShift
    std::vector<u8> discTable;   // max_disc Einträge; != 0 = Slot belegt
};

// Prüft Magic, Größen und berechnet max_disc wie libwbfs; sector0 muss den ganzen Kopf enthalten (12 + max_disc).
Result<WbfsPartHead, FragError> parseWbfsPartHead(std::span<const u8> sector0);
std::vector<u32> usedSlots(const WbfsPartHead& h);
// Byte-Offset der Disc-Info des Slots ab Partitionsanfang: (1 + slot × disc_info_lba) << hdSecShift.
u64 slotInfoOffset(const WbfsPartHead& h, u32 slot);
// Größe einer Disc-Info: ALIGN_LBA(0x100 + 2 × n_wbfs_sec_per_disc).
u32 wbfsDiscInfoBytes(const WbfsPartHead& h);
// Disc-Info → Abbildung; ein wlba hinter dem Partitionsende macht den Slot kaputt.
Result<WbfsLayout, FragError> parseWbfsDiscInfo(const WbfsPartHead& h, std::span<const u8> info);
// Die Partition als eine „Datei“ ab partLba (Gerätesektoren); Extents in 512-Byte-Sektoren.
Result<FilePart, FragError> wbfsPartitionFile(const WbfsPartHead& h, u64 partLba, u32 deviceSectorBytes);

// Spielpfad "wbfs:<slot>" (dezimal) → Slot.
std::optional<u32> wbfsSlotOf(std::string_view path);

struct WbfsScan {
    std::vector<IndexGame> games;     // Pfad "wbfs:<slot>", Schlüssel = ID6
    std::vector<u32> skippedSlots;    // kaputter Disc-Kopf oder kaputte Disc-Info (Review Focus 3)
};

// Liest Kopf und je belegtem Slot die Disc-Info (Disc-Kopf-Kopie: ID6, Titel). Kaputter Partitionskopf → Fehler.
Result<WbfsScan, FragError> scanWbfsPartition(BlockDevice& dev, u64 partLba, const MetaDb* meta);
// Fragmentliste eines belegten Slots; freier Slot oder Slot ≥ max_disc → WbfsInvalid.
Result<FragList, FragError> wbfsSlotFrags(BlockDevice& dev, u64 partLba, u32 slot);

}  // namespace nl
