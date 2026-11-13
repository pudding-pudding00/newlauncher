#pragma once
// WBFS-Datei → Disc-Abbildung (M9 Task 3): liest den Kopf einer .wbfs-Datei (wbfs_head_t in Sektor 0,
// Disc-Info ab Byte 512) und liefert, welcher Disc-Bereich an welchem Dateioffset liegt.

#include "core/result.hpp"
#include "core/types.hpp"

#include <span>
#include <vector>

namespace nl {

enum class FragError : u8 { WbfsInvalid, PartMissing, TooFragmented, OutOfFile };

struct WbfsLayout {
    u32 wbfsSecSize = 0;     // Bytes je WBFS-Block
    std::vector<u16> wlba;   // Disc-Block i liegt in Dateiblock wlba[i]; 0 = nicht belegt
};

// Bytes, die für parseWbfsHead gelesen werden müssen: Kopfsektor + ALIGN_LBA(0x100 + 2 × n_wbfs_sec_per_disc).
u32 wbfsHeadBytes(u8 wbfsSecSzShift);

// Prüft Magic „WBFS“, 512-Byte-Sektoren (hd_sec_sz_s = 9) und 15 ≤ wbfs_sec_sz_s ≤ 30.
// slot: Disc-Info ab 512 + slot × ALIGN_LBA(…); bisheriger Aufruf = Slot 0 einer .wbfs-Datei.
Result<WbfsLayout, FragError> parseWbfsHead(std::span<const u8> head, u32 slot = 0);

struct DiscRun {
    u64 discOffset;
    u64 fileOffset;
    u64 bytes;
};

// Belegte Blöcke in Disc-Reihenfolge; Nachbarn, die auch in der Datei aufeinander folgen, werden verschmolzen.
std::vector<DiscRun> discRuns(const WbfsLayout& layout);

}  // namespace nl
