#pragma once
// Testhilfe für M12: Partitionstabellen (MBR, EBR, GPT) und Dateisystem-Kennungen in einem Speicherabbild.

#include "core/crc32.hpp"
#include "support/fat_image.hpp"

#include <cstring>
#include <vector>

namespace nl::test {

// Abbild im Speicher; nicht beschriebene Sektoren lesen sich als Nullen.
using MemBlockDevice = SparseDisk;

inline void put64(SparseDisk& d, u32 lba, u32 off, u64 v) {
    d.put32(lba, off, u32(v));
    d.put32(lba, off + 4, u32(v >> 32));
}

// Schreibt `len` Byte an eine Byteposition relativ zum Sektor `lba`, auch über Sektorgrenzen.
inline void putBytes(SparseDisk& d, u32 lba, u32 byteOff, const void* src, u32 len) {
    const u32 ss = d.sectorSize();
    const auto* p = static_cast<const u8*>(src);
    for (u32 i = 0; i < len; ++i) {
        const u32 pos = byteOff + i;
        d.sector(lba + pos / ss)[pos % ss] = p[i];
    }
}

// FAT32-Bootsektor mit gültigem BPB (für die Erkennung genügt der Kopf).
inline void writeFatBootAt(SparseDisk& d, u32 lba) {
    u8* s = d.sector(lba);
    s[0] = 0xEB; s[1] = 0x58; s[2] = 0x90;
    std::memcpy(s + 3, "MSWIN4.1", 8);
    d.put16(lba, 0x0B, u16(d.sectorSize()));
    s[0x0D] = 8;
    d.put16(lba, 0x0E, 32);
    s[0x10] = 2;
    d.put32(lba, 0x20, 600000);
    d.put32(lba, 0x24, 600);
    std::memcpy(s + 0x52, "FAT32   ", 8);
    s[510] = 0x55;
    s[511] = 0xAA;
}

inline void writeOemBoot(SparseDisk& d, u32 lba, const char (&oem)[9]) {
    u8* s = d.sector(lba);
    s[0] = 0xEB; s[1] = 0x76; s[2] = 0x90;
    std::memcpy(s + 3, oem, 8);
    s[510] = 0x55;
    s[511] = 0xAA;
}
inline void writeNtfsBootAt(SparseDisk& d, u32 lba) { writeOemBoot(d, lba, "NTFS    "); }
inline void writeExFatBootAt(SparseDisk& d, u32 lba) { writeOemBoot(d, lba, "EXFAT   "); }

// ext2/3/4: Superblock bei Byte 1024 der Partition, Magic 0xEF53 an Offset 56.
inline void writeExtSuperblock(SparseDisk& d, u32 partLba) {
    const u8 magic[2] = {0x53, 0xEF};
    putBytes(d, partLba, 1024 + 56, magic, 2);
}

inline void writeWbfsHead(SparseDisk& d, u32 lba) { std::memcpy(d.sector(lba), "WBFS", 4); }

// EBR an `ebrLba`: Eintrag 0 = logische Partition (relativ zum EBR), Eintrag 1 = nächster EBR
// (relativ zum Anfang der erweiterten Partition, 0 = Ende).
inline void writeEbr(SparseDisk& d, u32 ebrLba, u32 logicalRel, u32 logicalSectors, u32 nextRel, u32 nextSectors) {
    u8* s = d.sector(ebrLba);
    s[0x1BE + 4] = 0x0C;
    d.put32(ebrLba, 0x1BE + 8, logicalRel);
    d.put32(ebrLba, 0x1BE + 12, logicalSectors);
    if (nextRel != 0) {
        s[0x1CE + 4] = 0x05;
        d.put32(ebrLba, 0x1CE + 8, nextRel);
        d.put32(ebrLba, 0x1CE + 12, nextSectors);
    }
    s[510] = 0x55;
    s[511] = 0xAA;
}

struct GptEntry {
    u64 first;
    u64 last;  // einschließlich
};

// Schutz-MBR, GPT-Kopf an LBA 1, 128 Einträge à 128 Byte ab LBA 2, beide CRCs korrekt.
inline void writeGpt(SparseDisk& d, const std::vector<GptEntry>& entries, u64 deviceSectors) {
    writeMbrEntry(d, 0, 0xEE, 1, u32(deviceSectors > 0xFFFFFFFFull ? 0xFFFFFFFFull : deviceSectors - 1));
    constexpr u32 kCount = 128, kSize = 128;
    std::vector<u8> table(kCount * kSize, 0);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        u8* e = table.data() + i * kSize;
        e[0] = 0xA2; e[1] = 0xA0; e[2] = 0xD0; e[3] = 0xEB;  // Basic Data, nur ≠ 0 zählt
        e[16] = u8(i + 1);
        for (int b = 0; b < 8; ++b) {
            e[32 + b] = u8(entries[i].first >> (8 * b));
            e[40 + b] = u8(entries[i].last >> (8 * b));
        }
    }
    putBytes(d, 2, 0, table.data(), u32(table.size()));

    std::vector<u8> head(92, 0);
    std::memcpy(head.data(), "EFI PART", 8);
    auto p32 = [&](u32 off, u32 v) { for (int b = 0; b < 4; ++b) head[off + b] = u8(v >> (8 * b)); };
    auto p64 = [&](u32 off, u64 v) { for (int b = 0; b < 8; ++b) head[off + b] = u8(v >> (8 * b)); };
    p32(8, 0x00010000);
    p32(12, 92);
    p64(24, 1);
    p64(32, deviceSectors - 1);
    p64(40, 34);
    p64(48, deviceSectors - 34);
    p64(72, 2);
    p32(80, kCount);
    p32(84, kSize);
    p32(88, crc32(table));
    p32(16, crc32(head));
    putBytes(d, 1, 0, head.data(), u32(head.size()));
}

}  // namespace nl::test
