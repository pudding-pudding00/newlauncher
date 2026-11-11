#include "core/fat_chain.hpp"

#include <array>

namespace nl {
namespace {

constexpr u32 kSector = 512;
constexpr u32 kEntryMask = 0x0FFFFFFF;
constexpr u32 kBadCluster = 0x0FFFFFF7;
constexpr u32 kEndOfChain = 0x0FFFFFF8;  // ab hier Kettenende

u16 le16(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }
u32 le32(const u8* p) { return u32{p[0]} | (u32{p[1]} << 8) | (u32{p[2]} << 16) | (u32{p[3]} << 24); }

using Sector = std::array<u8, kSector>;

bool hasSignature(const Sector& s) { return s[510] == 0x55 && s[511] == 0xAA; }

// Bootsektor-Prüfung nach Plan: Signatur, 512 Byte/Sektor, Sektoren/Cluster Zweierpotenz ≤ 128, ≥ 1 FAT.
// Dazu reservierte Sektoren und Gesamtgröße ≠ 0, sonst ergäbe die Geometrie keinen Sinn.
bool isFatBoot(const Sector& s) {
    if (!hasSignature(s) || le16(&s[0x0B]) != kSector) return false;
    const u8 spc = s[0x0D];
    if (spc == 0 || (spc & (spc - 1)) != 0) return false;
    if (s[0x10] == 0 || le16(&s[0x0E]) == 0) return false;
    return le16(&s[0x13]) != 0 || le32(&s[0x20]) != 0;
}

Result<FatVolume, FatError> volumeAt(const Sector& s, u32 lba) {
    using R = Result<FatVolume, FatError>;
    const u32 reserved = le16(&s[0x0E]);
    const u32 fats = s[0x10];
    const u32 rootDirSectors = (u32{le16(&s[0x11])} * 32 + kSector - 1) / kSector;
    const u32 fatSz = le16(&s[0x16]) != 0 ? le16(&s[0x16]) : le32(&s[0x24]);
    const u32 total = le16(&s[0x13]) != 0 ? le16(&s[0x13]) : le32(&s[0x20]);
    const u64 meta = u64{reserved} + u64{fats} * fatSz + rootDirSectors;
    if (fatSz == 0 || meta >= total) return R::err(FatError::NoFatVolume);
    FatVolume v;
    v.partLba = lba;
    v.fatLba = lba + reserved;
    v.dataLba = static_cast<u32>(lba + meta);
    v.sectorsPerCluster = s[0x0D];
    v.clusterCount = static_cast<u32>((total - meta) / v.sectorsPerCluster);
    // Microsoft-Regel: der FAT-Typ folgt allein aus der Clusterzahl.
    if (v.clusterCount < 65525) return R::err(FatError::NotFat32);
    return R::ok(v);
}

}  // namespace

Result<FatVolume, FatError> findFatVolume(BlockDevice& dev) {
    using R = Result<FatVolume, FatError>;
    if (dev.sectorSize() != kSector) return R::err(FatError::SectorSize);
    Sector s{};
    if (!dev.read(0, 1, s)) return R::err(FatError::Io);
    if (isFatBoot(s)) return volumeAt(s, 0);
    if (!hasSignature(s)) return R::err(FatError::NoFatVolume);

    std::array<u32, 4> starts{};
    for (u32 i = 0; i < 4; ++i) {
        const u8* e = &s[0x1BE + 16 * i];
        starts[i] = e[4] != 0 ? le32(e + 8) : 0;
    }
    for (u32 lba : starts) {
        if (lba == 0) continue;
        Sector b{};
        if (!dev.read(lba, 1, b)) return R::err(FatError::Io);
        if (isFatBoot(b)) return volumeAt(b, lba);
    }
    return R::err(FatError::NoFatVolume);
}

Result<std::vector<Extent>, FatError> fileExtents(BlockDevice& dev, const FatVolume& vol, u32 startCluster, u64 fileSize) {
    using R = Result<std::vector<Extent>, FatError>;
    std::vector<Extent> out;
    if (fileSize == 0) return R::ok(std::move(out));

    const u64 clusterBytes = u64{vol.sectorsPerCluster} * kSector;
    const u64 need = (fileSize + clusterBytes - 1) / clusterBytes;
    if (need > vol.clusterCount) return R::err(FatError::BadChain);  // größer als das Volume

    const auto valid = [&](u32 c) { return c >= 2 && c < vol.clusterCount + 2; };
    Sector cache{};
    u32 cachedLba = 0;
    bool cached = false;
    const auto next = [&](u32 c, u32& value) {
        const u32 lba = vol.fatLba + (c * 4) / kSector;
        if (!cached || cachedLba != lba) {
            if (!dev.read(lba, 1, cache)) return false;
            cachedLba = lba;
            cached = true;
        }
        value = le32(&cache[(c * 4) % kSector]) & kEntryMask;
        return true;
    };

    u32 c = startCluster;
    for (u64 i = 0; i < need; ++i) {
        if (!valid(c)) return R::err(FatError::BadChain);
        const u32 lba = vol.dataLba + (c - 2) * vol.sectorsPerCluster;
        if (!out.empty() && out.back().lba + out.back().count == lba)
            out.back().count += vol.sectorsPerCluster;
        else
            out.push_back({static_cast<u32>(i * vol.sectorsPerCluster), lba, vol.sectorsPerCluster});

        u32 n = 0;
        if (!next(c, n)) return R::err(FatError::Io);
        const bool last = i + 1 == need;
        if (n >= kEndOfChain) {
            if (!last) return R::err(FatError::ChainTooShort);
        } else if (last || n == kBadCluster) {
            return R::err(FatError::BadChain);  // Kette länger als die Datei (auch jede Schleife) oder defekter Cluster
        }
        c = n;
    }
    return R::ok(std::move(out));
}

}  // namespace nl
