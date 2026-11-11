#include "core/wbfs_map.hpp"

#include <cstring>

namespace nl {
namespace {

constexpr u32 kHdSector = 512;
constexpr u32 kDiscHeaderCopy = 0x100;      // wbfs_disc_info_t::disc_header_copy
constexpr u32 kWiiSectorsPerDisc = 143432 * 2;  // libwbfs.c:82, zweilagige Disc
constexpr u8 kWiiSecShift = 15;              // 0x8000

u32 sectorsPerDisc(u8 shift) { return kWiiSectorsPerDisc >> (shift - kWiiSecShift); }

}  // namespace

u32 wbfsHeadBytes(u8 shift) {
    const u32 info = kDiscHeaderCopy + 2 * sectorsPerDisc(shift);
    return kHdSector + (info + kHdSector - 1) / kHdSector * kHdSector;  // ALIGN_LBA wie libwbfs.c:120
}

Result<WbfsLayout, FragError> parseWbfsHead(std::span<const u8> head) {
    using R = Result<WbfsLayout, FragError>;
    if (head.size() < kHdSector || std::memcmp(head.data(), "WBFS", 4) != 0) return R::err(FragError::WbfsInvalid);
    const u8 hdShift = head[8];
    const u8 secShift = head[9];
    // 2^30 = 1 GiB je Block ist mehr als jedes Werkzeug schreibt; größer ließe wbfsSecSize überlaufen.
    if (hdShift != 9 || secShift < kWiiSecShift || secShift > 30) return R::err(FragError::WbfsInvalid);
    if (head.size() < wbfsHeadBytes(secShift)) return R::err(FragError::WbfsInvalid);

    WbfsLayout l;
    l.wbfsSecSize = u32{1} << secShift;
    const u32 n = sectorsPerDisc(secShift);
    l.wlba.resize(n);
    const u8* t = head.data() + kHdSector + kDiscHeaderCopy;
    for (u32 i = 0; i < n; ++i) l.wlba[i] = static_cast<u16>((t[2 * i] << 8) | t[2 * i + 1]);
    return R::ok(std::move(l));
}

std::vector<DiscRun> discRuns(const WbfsLayout& layout) {
    std::vector<DiscRun> out;
    const u64 s = layout.wbfsSecSize;
    for (std::size_t i = 0; i < layout.wlba.size(); ++i) {
        if (layout.wlba[i] == 0) continue;
        const DiscRun r{i * s, layout.wlba[i] * s, s};
        if (!out.empty() && out.back().discOffset + out.back().bytes == r.discOffset &&
            out.back().fileOffset + out.back().bytes == r.fileOffset)
            out.back().bytes += s;
        else
            out.push_back(r);
    }
    return out;
}

}  // namespace nl
