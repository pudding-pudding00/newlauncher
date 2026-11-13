#include "core/wbfs_part.hpp"

#include "core/disc_header.hpp"

#include <algorithm>
#include <cstring>
#include <set>

namespace nl {
namespace {

constexpr u32 kHeadFixed = 12;               // wbfs_head_t ohne disc_table
constexpr u32 kDiscHeaderCopy = 0x100;       // wbfs_disc_info_t::disc_header_copy
constexpr u32 kWiiSectorsPerDisc = 143432 * 2;
constexpr u8 kWiiSecShift = 15;
constexpr u32 kMaxHeadBytes = 4096;          // größter hd-Sektor (hd_sec_sz_s ≤ 12)

using HeadR = Result<WbfsPartHead, FragError>;

u32 be32(const u8* p) { return (u32{p[0]} << 24) | (u32{p[1]} << 16) | (u32{p[2]} << 8) | p[3]; }

u32 perDisc(u8 wbfsShift) { return kWiiSectorsPerDisc >> (wbfsShift - kWiiSecShift); }

u32 alignHd(u32 bytes, u8 hdShift) {
    const u32 hd = u32{1} << hdShift;
    return (bytes + hd - 1) / hd * hd;
}

u32 nWbfsSec(const WbfsPartHead& h) { return h.nHdSec >> (h.wbfsSecShift - h.hdSecShift); }

// `len` Byte ab Byte `off` der Partition; Gerätesektoren beliebiger Größe (512 oder 4096).
bool readBytes(BlockDevice& dev, u64 partLba, u64 off, u32 len, std::vector<u8>& out) {
    const u32 ss = dev.sectorSize();
    if (ss == 0) return false;
    const u64 first = partLba + off / ss;
    const u64 last = partLba + (off + len + ss - 1) / ss;  // exklusiv
    if (last > 0xFFFFFFFFull + 1) return false;            // BlockDevice adressiert mit u32
    const u32 count = static_cast<u32>(last - first);
    std::vector<u8> buf(std::size_t{count} * ss);
    if (!dev.read(static_cast<u32>(first), count, buf)) return false;
    const std::size_t skip = static_cast<std::size_t>(off % ss);
    out.assign(buf.begin() + static_cast<std::ptrdiff_t>(skip), buf.begin() + static_cast<std::ptrdiff_t>(skip + len));
    return true;
}

HeadR readHead(BlockDevice& dev, u64 partLba) {
    std::vector<u8> head;
    if (!readBytes(dev, partLba, 0, kMaxHeadBytes, head)) return HeadR::err(FragError::WbfsInvalid);
    return parseWbfsPartHead(head);
}

Result<WbfsLayout, FragError> readSlot(BlockDevice& dev, u64 partLba, const WbfsPartHead& h, u32 slot,
                                       std::vector<u8>& info) {
    using R = Result<WbfsLayout, FragError>;
    if (slot >= h.discTable.size() || h.discTable[slot] == 0) return R::err(FragError::WbfsInvalid);
    if (!readBytes(dev, partLba, slotInfoOffset(h, slot), wbfsDiscInfoBytes(h), info)) return R::err(FragError::WbfsInvalid);
    return parseWbfsDiscInfo(h, info);
}

}  // namespace

HeadR parseWbfsPartHead(std::span<const u8> s) {
    if (s.size() < kHeadFixed || std::memcmp(s.data(), "WBFS", 4) != 0) return HeadR::err(FragError::WbfsInvalid);
    WbfsPartHead h{be32(s.data() + 4), s[8], s[9], {}};
    // libwbfs schreibt 512- oder 4096-Byte-Sektoren; Blöcke ab einem Wii-Sektor (32 KiB) bis 1 GiB.
    if (h.hdSecShift < 9 || h.hdSecShift > 12 || h.wbfsSecShift < kWiiSecShift || h.wbfsSecShift > 30 || h.nHdSec == 0)
        return HeadR::err(FragError::WbfsInvalid);
    const u32 blocks = nWbfsSec(h);
    const u32 secSz = u32{1} << h.wbfsSecShift;
    // Block 0 ist der Kopf; wlba ist u16, mehr Blöcke kann keine Disc-Info adressieren.
    if (blocks < 2 || blocks > 0xFFFF || blocks / 8 >= secSz) return HeadR::err(FragError::WbfsInvalid);
    const u32 freeblksLba = (secSz - blocks / 8) >> h.hdSecShift;
    const u32 infoLba = wbfsDiscInfoBytes(h) >> h.hdSecShift;
    u32 maxDisc = freeblksLba > 0 ? (freeblksLba - 1) / infoLba : 0;
    maxDisc = std::min(maxDisc, (u32{1} << h.hdSecShift) - kHeadFixed);  // wie wbfs_open_partition
    if (maxDisc == 0 || s.size() < kHeadFixed + maxDisc) return HeadR::err(FragError::WbfsInvalid);
    h.discTable.assign(s.begin() + kHeadFixed, s.begin() + kHeadFixed + maxDisc);
    return HeadR::ok(std::move(h));
}

std::vector<u32> usedSlots(const WbfsPartHead& h) {
    std::vector<u32> out;
    for (u32 i = 0; i < h.discTable.size(); ++i)
        if (h.discTable[i] != 0) out.push_back(i);
    return out;
}

u32 wbfsDiscInfoBytes(const WbfsPartHead& h) {
    return alignHd(kDiscHeaderCopy + 2 * perDisc(h.wbfsSecShift), h.hdSecShift);
}

u64 slotInfoOffset(const WbfsPartHead& h, u32 slot) {
    const u64 infoLba = wbfsDiscInfoBytes(h) >> h.hdSecShift;
    return (1 + u64{slot} * infoLba) << h.hdSecShift;
}

Result<WbfsLayout, FragError> parseWbfsDiscInfo(const WbfsPartHead& h, std::span<const u8> info) {
    using R = Result<WbfsLayout, FragError>;
    const u32 n = perDisc(h.wbfsSecShift);
    if (info.size() < kDiscHeaderCopy + 2 * n) return R::err(FragError::WbfsInvalid);
    WbfsLayout l;
    l.wbfsSecSize = u32{1} << h.wbfsSecShift;
    l.wlba.resize(n);
    const u32 blocks = nWbfsSec(h);
    bool any = false;
    const u8* t = info.data() + kDiscHeaderCopy;
    for (u32 i = 0; i < n; ++i) {
        const u16 w = static_cast<u16>((t[2 * i] << 8) | t[2 * i + 1]);
        if (w != 0 && w >= blocks) return R::err(FragError::WbfsInvalid);  // hinter dem Partitionsende
        any = any || w != 0;
        l.wlba[i] = w;
    }
    if (!any) return R::err(FragError::WbfsInvalid);
    return R::ok(std::move(l));
}

Result<FilePart, FragError> wbfsPartitionFile(const WbfsPartHead& h, u64 partLba, u32 deviceSectorBytes) {
    using R = Result<FilePart, FragError>;
    if (deviceSectorBytes < 512 || deviceSectorBytes % 512 != 0) return R::err(FragError::WbfsInvalid);
    const u64 bytes = u64{h.nHdSec} << h.hdSecShift;
    const u64 lba = partLba * (deviceSectorBytes / 512);
    const u64 sectors = bytes / 512;
    if (lba + sectors > 0xFFFFFFFFull) return R::err(FragError::WbfsInvalid);  // Fragmentliste adressiert mit u32
    return R::ok(FilePart{bytes, {Extent{0, static_cast<u32>(lba), static_cast<u32>(sectors)}}});
}

std::optional<u32> wbfsSlotOf(std::string_view path) {
    constexpr std::string_view kPrefix = "wbfs:";
    if (!path.starts_with(kPrefix)) return std::nullopt;
    const std::string_view digits = path.substr(kPrefix.size());
    if (digits.empty() || digits.size() > 5) return std::nullopt;
    u32 v = 0;
    for (char c : digits) {
        if (c < '0' || c > '9') return std::nullopt;
        v = v * 10 + static_cast<u32>(c - '0');
    }
    return v;
}

Result<WbfsScan, FragError> scanWbfsPartition(BlockDevice& dev, u64 partLba, const MetaDb* meta) {
    using R = Result<WbfsScan, FragError>;
    auto head = readHead(dev, partLba);
    if (!head.isOk()) return R::err(head.error());
    const WbfsPartHead& h = head.value();
    WbfsScan out;
    std::set<std::string> keys;
    std::vector<u8> info;
    for (u32 slot : usedSlots(h)) {
        auto layout = readSlot(dev, partLba, h, slot, info);
        std::optional<DiscInfo> disc;
        if (layout.isOk()) disc = parseWiiIso(std::span<const u8>(info).first(kDiscHeaderCopy));
        if (!disc || !keys.insert(disc->id6).second) {  // kaputt oder dieselbe ID6 in zwei Slots
            out.skippedSlots.push_back(slot);
            continue;
        }
        u64 used = 0;
        for (u16 w : layout.value().wlba) used += w != 0 ? 1 : 0;
        IndexGame g{Console::Wii, 0, disc->id6, disc->title, "wbfs:" + std::to_string(slot),
                    used * layout.value().wbfsSecSize};
        if (!g.title.empty()) g.flags |= kFlagTitleFromHeader;
        else g.title = g.key;
        if (meta != nullptr) {
            auto e = meta->find(metaKey(g));
            if (e && !e->title.empty()) {
                g.title = std::string(e->title);
                g.flags |= kFlagTitleFromMeta;
            }
        }
        out.games.push_back(std::move(g));
    }
    return R::ok(std::move(out));
}

Result<FragList, FragError> wbfsSlotFrags(BlockDevice& dev, u64 partLba, u32 slot) {
    using R = Result<FragList, FragError>;
    auto head = readHead(dev, partLba);
    if (!head.isOk()) return R::err(head.error());
    std::vector<u8> info;
    auto layout = readSlot(dev, partLba, head.value(), slot, info);
    if (!layout.isOk()) return R::err(layout.error());
    auto file = wbfsPartitionFile(head.value(), partLba, dev.sectorSize());
    if (!file.isOk()) return R::err(file.error());
    return buildWbfsFragList(layout.value(), std::span<const FilePart>(&file.value(), 1));
}

}  // namespace nl
