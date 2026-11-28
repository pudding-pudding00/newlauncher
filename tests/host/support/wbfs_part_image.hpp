#pragma once
// Testhilfe für M16 Task 5: WBFS-Partition in einem Speicherabbild (Layout wie libwbfs: wbfs_head_t im ersten
// hd-Sektor, Disc-Tabelle ab Byte 12, Disc-Info je Slot ab hd-Sektor 1 + Slot × disc_info_lba).

#include "support/disc_images.hpp"
#include "support/disk_image.hpp"

#include <string>
#include <vector>

namespace nl::test {

struct WbfsSlotSpec {
    std::string id6;          // leer = Slot frei (disc_table 0)
    std::string title;
    std::vector<u16> wlba;    // Disc-Block i liegt im Partitionsblock wlba[i]
    bool brokenHeader = false;  // Disc-Kopf ohne Wii-Magic
};

struct WbfsPartSpec {
    u8 hdShift = 9;
    u8 wbfsShift = 21;
    u32 nWbfsSec = 100;  // Partitionsgröße in WBFS-Blöcken
};

// Unabhängig von core/wbfs_part nachgerechnet (libwbfs.c: ALIGN_LBA auf hd_sec_sz).
inline u32 testDiscInfoLba(const WbfsPartSpec& p) {
    const u32 perDisc = (143432u * 2) >> (p.wbfsShift - 15);
    const u32 bytes = 0x100 + 2 * perDisc;
    const u32 hd = 1u << p.hdShift;
    return (bytes + hd - 1) / hd;
}

// Schreibt die Partition ab `partLba` (Gerätesektoren); Gerätesektor = hd-Sektor.
inline void writeWbfsPartition(SparseDisk& d, u32 partLba, const std::vector<WbfsSlotSpec>& slots,
                               WbfsPartSpec p = {}) {
    const u32 hd = 1u << p.hdShift;
    const u32 nHdSec = p.nWbfsSec << (p.wbfsShift - p.hdShift);
    std::vector<u8> head(hd, 0);
    std::memcpy(head.data(), "WBFS", 4);
    putBe32(head, 4, nHdSec);
    head[8] = p.hdShift;
    head[9] = p.wbfsShift;
    for (std::size_t i = 0; i < slots.size(); ++i) head[12 + i] = slots[i].id6.empty() ? 0 : 1;
    putBytes(d, partLba, 0, head.data(), hd);

    const u32 infoLba = testDiscInfoLba(p);
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const WbfsSlotSpec& s = slots[i];
        if (s.id6.empty()) continue;
        std::vector<u8> info(std::size_t{infoLba} * hd, 0);
        putHeader(info, 0, s.id6, s.title, true);
        if (s.brokenHeader) info[0x18] = 0;
        for (std::size_t b = 0; b < s.wlba.size(); ++b) {
            info[0x100 + 2 * b] = static_cast<u8>(s.wlba[b] >> 8);
            info[0x100 + 2 * b + 1] = static_cast<u8>(s.wlba[b]);
        }
        const u64 byteOff = (1 + u64{i} * infoLba) * hd;
        putBytes(d, partLba, static_cast<u32>(byteOff), info.data(), static_cast<u32>(info.size()));
    }
}

// Die ersten `bytes` Byte der Partition als Puffer (für parseWbfsHead(head, slot)).
inline std::vector<u8> readPrefix(SparseDisk& d, u32 partLba, u32 bytes) {
    const u32 ss = d.sectorSize();
    const std::size_t rounded = (std::size_t{bytes} + ss - 1) / ss * ss;
    std::vector<u8> out(rounded);
    d.read(partLba, static_cast<u32>(out.size() / ss), out);
    out.resize(bytes);
    return out;
}

}  // namespace nl::test
