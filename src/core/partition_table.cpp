#include "core/partition_table.hpp"

#include "core/crc32.hpp"

#include <cstring>

namespace nl {
namespace {

using R = Result<std::vector<PartitionInfo>>;

constexpr u32 kMaxEbrLinks = 32;
constexpr u32 kMaxGptEntries = 128;
constexpr u32 kMaxGptTableBytes = 64 * 1024;  // CRC über die ganze Tabelle; mehr kommt in der Praxis nicht vor
constexpr u32 kProbeBytes = 2048;             // deckt Bootsektor und ext-Superblock (Byte 1024–2047) ab
constexpr u64 kMaxLba = 0xFFFFFFFFull;        // BlockDevice::read adressiert mit u32

u16 le16(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }
u32 le32(const u8* p) { return u32{p[0]} | (u32{p[1]} << 8) | (u32{p[2]} << 16) | (u32{p[3]} << 24); }
u64 le64(const u8* p) { return u64{le32(p)} | (u64{le32(p + 4)} << 32); }

R invalid(const char* why) { return R::err(Error{ErrorCode::InvalidData, why}); }
R ioError(const char* why) { return R::err(Error{ErrorCode::IoError, why}); }

bool isPow2(u32 v) { return v != 0 && (v & (v - 1)) == 0; }

bool hasSignature(std::span<const u8> s) { return s.size() >= 512 && s[510] == 0x55 && s[511] == 0xAA; }

// FAT12/16/32-BPB: Signatur, Sektorgröße 512–4096, Sektoren/Cluster Zweierpotenz, ≥ 1 FAT, Geometrie ≠ 0.
bool isFatBpb(std::span<const u8> s) {
    if (!hasSignature(s)) return false;
    const u32 bps = le16(&s[0x0B]);
    if (!isPow2(bps) || bps < 512 || bps > 4096) return false;
    if (!isPow2(s[0x0D]) || s[0x10] == 0 || le16(&s[0x0E]) == 0) return false;
    const bool hasFatSize = le16(&s[0x16]) != 0 || le32(&s[0x24]) != 0;
    return hasFatSize && (le16(&s[0x13]) != 0 || le32(&s[0x20]) != 0);
}

class Reader {
public:
    Reader(BlockDevice& dev, u64 deviceSectors) : dev_(dev), ss_(dev.sectorSize()), deviceSectors_(deviceSectors) {}

    u32 sectorSize() const { return ss_; }

    // Liest `count` Sektoren ab `lba` in `buf` (wird passend vergrößert).
    bool read(u64 lba, u32 count, std::vector<u8>& buf) {
        if (lba + count - 1 > kMaxLba) return false;
        buf.assign(std::size_t{count} * ss_, 0);
        return dev_.read(static_cast<u32>(lba), count, buf);
    }

    // Prüft Lage und Länge, bestimmt die Art und hängt die Partition an; false, wenn die Liste voll ist.
    bool add(std::vector<PartitionInfo>& out, u64 lba, u64 sectors, u8 type, bool logical) {
        if (out.size() >= kMaxPartitions) return false;
        if (sectors == 0 || lba == 0 || lba + sectors > deviceSectors_ || lba + sectors - 1 > kMaxLba) return true;
        const u32 count = (kProbeBytes + ss_ - 1) / ss_;
        FsKind kind = FsKind::Unknown;
        // Ein unlesbarer Partitionsanfang macht nur diese Partition unbekannt, nicht die ganze Tabelle.
        if (read(lba, count, probe_)) kind = detectFs(probe_);
        out.push_back(PartitionInfo{lba, sectors, type, kind, logical});
        return true;
    }

private:
    BlockDevice& dev_;
    u32 ss_;
    u64 deviceSectors_;
    std::vector<u8> probe_;
};

bool isExtended(u8 type) { return type == 0x05 || type == 0x0F; }

void readEbrChain(Reader& rd, std::vector<PartitionInfo>& out, u64 extStart, u64 extSectors) {
    std::vector<u64> visited;
    std::vector<u8> s;
    u64 ebr = extStart;
    for (u32 link = 0; link < kMaxEbrLinks; ++link) {
        if (ebr < extStart || ebr >= extStart + extSectors) return;
        for (u64 v : visited)
            if (v == ebr) return;
        visited.push_back(ebr);
        if (!rd.read(ebr, 1, s) || !hasSignature(s)) return;
        const u8* logical = &s[0x1BE];
        const u8* next = &s[0x1CE];
        if (logical[4] != 0 && !isExtended(logical[4]))
            if (!rd.add(out, ebr + le32(logical + 8), le32(logical + 12), logical[4], true)) return;
        if (!isExtended(next[4]) || le32(next + 8) == 0) return;
        ebr = extStart + le32(next + 8);
    }
}

R readGpt(Reader& rd) {
    const u32 ss = rd.sectorSize();
    std::vector<u8> head;
    if (!rd.read(1, 1, head)) return ioError("GPT-Kopf nicht lesbar");
    if (std::memcmp(head.data(), "EFI PART", 8) != 0) return invalid("GPT-Signatur fehlt");
    const u32 headSize = le32(&head[12]);
    if (headSize < 92 || headSize > ss) return invalid("GPT-Kopfgröße ungültig");
    const u32 headCrc = le32(&head[16]);
    std::memset(&head[16], 0, 4);
    if (crc32(std::span<const u8>(head.data(), headSize)) != headCrc) return invalid("GPT-Kopf-CRC falsch");

    const u64 entriesLba = le64(&head[72]);
    const u32 count = le32(&head[80]);
    const u32 entrySize = le32(&head[84]);
    if (entrySize < 128 || entrySize % 8 != 0 || count == 0) return invalid("GPT-Einträge ungültig");
    const u64 tableBytes = u64{count} * entrySize;
    if (tableBytes > kMaxGptTableBytes) return invalid("GPT-Tabelle zu groß");
    std::vector<u8> table;
    const u32 sectors = static_cast<u32>((tableBytes + ss - 1) / ss);
    if (!rd.read(entriesLba, sectors, table)) return ioError("GPT-Einträge nicht lesbar");
    const std::span<const u8> used(table.data(), static_cast<std::size_t>(tableBytes));
    if (crc32(used) != le32(&head[88])) return invalid("GPT-Einträge-CRC falsch");

    std::vector<PartitionInfo> out;
    for (u32 i = 0; i < count && i < kMaxGptEntries; ++i) {
        const u8* e = &table[std::size_t{i} * entrySize];
        bool empty = true;
        for (int b = 0; b < 16; ++b) empty = empty && e[b] == 0;
        if (empty) continue;
        const u64 first = le64(e + 32), last = le64(e + 40);
        if (last < first) continue;
        if (!rd.add(out, first, last - first + 1, 0xEE, false)) break;
    }
    return R::ok(std::move(out));
}

}  // namespace

FsKind detectFs(std::span<const u8> s) {
    if (s.size() >= 4 && std::memcmp(s.data(), "WBFS", 4) == 0) return FsKind::Wbfs;
    if (s.size() >= 11 && std::memcmp(&s[3], "NTFS    ", 8) == 0) return FsKind::Ntfs;
    if (s.size() >= 11 && std::memcmp(&s[3], "EXFAT   ", 8) == 0) return FsKind::ExFat;
    if (isFatBpb(s)) return FsKind::Fat;
    if (s.size() >= 1024 + 58 && le16(&s[1024 + 56]) == 0xEF53) return FsKind::Ext;
    return FsKind::Unknown;
}

Result<std::vector<PartitionInfo>> readPartitions(BlockDevice& dev, u64 deviceSectors) {
    Reader rd(dev, deviceSectors);
    if (rd.sectorSize() < 512) return invalid("Sektorgröße < 512");
    std::vector<u8> s0;
    if (!rd.read(0, 1, s0)) return ioError("Sektor 0 nicht lesbar");

    // Superfloppy: Dateisystem ohne Tabelle. ext zählt hier nicht, sein Magic läge nicht in Sektor 0.
    const FsKind whole = detectFs(std::span<const u8>(s0.data(), 512));
    if (whole != FsKind::Unknown && whole != FsKind::Ext) {
        std::vector<PartitionInfo> out{PartitionInfo{0, deviceSectors, 0, whole, false}};
        return R::ok(std::move(out));
    }
    if (!hasSignature(s0)) return invalid("keine Partitionstabelle");

    for (u32 i = 0; i < 4; ++i)
        if (s0[0x1BE + 16 * i + 4] == 0xEE) return readGpt(rd);

    std::vector<PartitionInfo> out;
    struct Ext { u64 lba, sectors; };
    std::vector<Ext> extended;
    for (u32 i = 0; i < 4; ++i) {
        const u8* e = &s0[0x1BE + 16 * i];
        if (e[4] == 0) continue;
        if (isExtended(e[4])) {
            extended.push_back(Ext{le32(e + 8), le32(e + 12)});
            continue;
        }
        if (!rd.add(out, le32(e + 8), le32(e + 12), e[4], false)) return R::ok(std::move(out));
    }
    for (const Ext& x : extended) readEbrChain(rd, out, x.lba, x.sectors);
    return R::ok(std::move(out));
}

}  // namespace nl
