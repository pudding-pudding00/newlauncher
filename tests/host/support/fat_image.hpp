#pragma once
// Testhilfe für M9: dünn besetzte Platte und ein FAT32-Bauer (MBR, BPB, FAT-Ketten).

#include "core/block_dev.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

namespace nl::test {

class SparseDisk : public BlockDevice {
public:
    explicit SparseDisk(u32 sectorSize = 512) : ss_(sectorSize) {}

    bool read(u32 lba, u32 count, std::span<u8> out) override {
        ++reads;
        if (out.size() < std::size_t{count} * ss_) return false;
        for (u32 i = 0; i < count; ++i) {
            auto it = sectors_.find(lba + i);
            u8* dst = out.data() + std::size_t{i} * ss_;
            if (it == sectors_.end()) std::memset(dst, 0, ss_);
            else std::memcpy(dst, it->second.data(), ss_);
        }
        return true;
    }
    u32 sectorSize() const override { return ss_; }

    // Sektor zum Beschreiben (legt ihn mit Nullen an).
    u8* sector(u32 lba) {
        auto& s = sectors_[lba];
        if (s.empty()) s.assign(ss_, 0);
        return s.data();
    }
    void put16(u32 lba, u32 off, u16 v) { u8* s = sector(lba); s[off] = u8(v); s[off + 1] = u8(v >> 8); }
    void put32(u32 lba, u32 off, u32 v) { put16(lba, off, u16(v)); put16(lba, off + 2, u16(v >> 16)); }

    u32 reads = 0;

private:
    u32 ss_;
    std::map<u32, std::vector<u8>> sectors_;
};

// Schreibt einen MBR-Eintrag (Index 0–3) und die Signatur.
inline void writeMbrEntry(SparseDisk& d, int index, u8 type, u32 lba, u32 sectors) {
    const u32 e = 0x1BE + 16u * u32(index);
    d.sector(0)[e + 4] = type;
    d.put32(0, e + 8, lba);
    d.put32(0, e + 12, sectors);
    d.sector(0)[510] = 0x55;
    d.sector(0)[511] = 0xAA;
}

// NTFS-Bootsektor: 512 Byte/Sektor, 8 Sektoren/Cluster, aber keine FATs.
inline void writeNtfsBoot(SparseDisk& d, u32 lba) {
    u8* s = d.sector(lba);
    const u8 head[] = {0xEB, 0x52, 0x90, 'N', 'T', 'F', 'S', ' ', ' ', ' ', ' '};
    std::memcpy(s, head, sizeof head);
    d.put16(lba, 0x0B, 512);
    s[0x0D] = 8;
    s[510] = 0x55;
    s[511] = 0xAA;
}

class Fat32Builder {
public:
    // fat16 = true schreibt einen FAT16-BPB (FATSz16, 512 Stammverzeichniseinträge); der FAT-Typ folgt
    // trotzdem nur aus der Clusterzahl.
    Fat32Builder(SparseDisk& d, u32 partLba, u8 sectorsPerCluster, u32 totalSectors, bool withMbr = true, bool fat16 = false)
        : d_(d), partLba_(partLba), spc_(sectorsPerCluster), fat16_(fat16) {
        const u32 reserved = fat16 ? 4 : 32;
        const u32 rootDirSectors = fat16 ? (512 * 32) / 512 : 0;
        const u32 entryBytes = fat16 ? 2 : 4;
        fatSz_ = ((totalSectors / spc_ + 2) * entryBytes + 511) / 512;
        fatLba = partLba + reserved;
        dataLba = fatLba + 2 * fatSz_ + rootDirSectors;
        clusterCount = (totalSectors - (reserved + 2 * fatSz_ + rootDirSectors)) / spc_;

        u8* s = d.sector(partLba);
        s[0] = 0xEB; s[1] = 0x58; s[2] = 0x90;
        std::memcpy(s + 3, "MSWIN4.1", 8);
        d.put16(partLba, 0x0B, 512);
        s[0x0D] = spc_;
        d.put16(partLba, 0x0E, u16(reserved));
        s[0x10] = 2;
        s[0x15] = 0xF8;
        if (fat16) {
            d.put16(partLba, 0x11, 512);
            d.put16(partLba, 0x16, u16(fatSz_));
            d.put32(partLba, 0x20, totalSectors);
        } else {
            d.put32(partLba, 0x20, totalSectors);
            d.put32(partLba, 0x24, fatSz_);
            d.put32(partLba, 0x2C, 2);
            std::memcpy(s + 0x52, "FAT32   ", 8);
        }
        s[510] = 0x55;
        s[511] = 0xAA;
        if (withMbr) writeMbrEntry(d, 0, fat16 ? 0x06 : 0x0C, partLba, totalSectors);
    }

    void setEntry(u32 cluster, u32 value) {
        const u32 off = cluster * 4;
        d_.put32(fatLba + off / 512, off % 512, value);
    }
    // Verkettet die Cluster in der angegebenen Reihenfolge, der letzte bekommt Kettenende.
    void setChain(const std::vector<u32>& clusters) {
        for (std::size_t i = 0; i < clusters.size(); ++i)
            setEntry(clusters[i], i + 1 < clusters.size() ? clusters[i + 1] : 0x0FFFFFFFu);
    }
    u32 clusterLba(u32 cluster) const { return dataLba + (cluster - 2) * spc_; }

    u32 fatLba = 0, dataLba = 0, clusterCount = 0;

private:
    SparseDisk& d_;
    u32 partLba_;
    u32 spc_;
    bool fat16_;
    u32 fatSz_ = 0;
};

}  // namespace nl::test
