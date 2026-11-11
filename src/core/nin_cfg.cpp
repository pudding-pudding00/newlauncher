#include "core/nin_cfg.hpp"

#include <algorithm>
#include <vector>

namespace nl {
namespace {

void put32(std::array<u8, kNinCfgSize>& b, std::size_t off, u32 v) {
    b[off] = static_cast<u8>(v >> 24);
    b[off + 1] = static_cast<u8>(v >> 16);
    b[off + 2] = static_cast<u8>(v >> 8);
    b[off + 3] = static_cast<u8>(v);
}

constexpr std::size_t kOffConfig = 0x008, kOffVideo = 0x00C, kOffLang = 0x010, kOffPath = 0x014,
                      kOffMaxPads = 0x214, kOffGameId = 0x218, kOffMemCardBlocks = 0x21C;

}  // namespace

Result<std::array<u8, kNinCfgSize>, NinCfgError> buildNinCfg(const NinConfigInput& in) {
    using R = Result<std::array<u8, kNinCfgSize>, NinCfgError>;
    if (in.gamePath.size() > kNinMaxGamePath) return R::err(NinCfgError::PathTooLong);
    if (in.id6.size() < 4) return R::err(NinCfgError::BadId);

    std::array<u8, kNinCfgSize> b{};
    put32(b, 0x000, kNinCfgMagic);
    put32(b, 0x004, kNinCfgVersion);
    put32(b, kOffConfig, kNinCfgMemcardEmu | kNinCfgAutoBoot | (in.usb ? kNinCfgUsb : 0));
    put32(b, kOffVideo, 0);            // NIN_VID_AUTO
    put32(b, kOffLang, 0xFFFFFFFFu);   // NIN_LAN_AUTO
    std::copy(in.gamePath.begin(), in.gamePath.end(), b.begin() + kOffPath);
    put32(b, kOffMaxPads, 4);
    for (std::size_t i = 0; i < 4; ++i) b[kOffGameId + i] = static_cast<u8>(in.id6[i]);
    b[kOffMemCardBlocks] = 2;  // 251 Blöcke
    return R::ok(b);
}

std::string ninGamePath(std::string_view relPath) {
    std::string out(relPath);
    std::replace(out.begin(), out.end(), '\\', '/');
    const auto first = out.find_first_not_of('/');
    return "/" + (first == std::string::npos ? std::string() : out.substr(first));
}

namespace {

constexpr std::string_view kVersionTag = "$$Version:";
constexpr std::size_t kScanBlock = 64 * 1024;
constexpr std::size_t kScanOverlap = 16;  // > Länge des Tags: ein Tag über der Grenze steht ganz im nächsten Block
constexpr std::size_t kNumberBytes = 16;  // "65535.65535" passt

// Liest bis zu 5 Ziffern; false, wenn keine da sind.
bool readNumber(std::string_view s, std::size_t& pos, u16& out) {
    u32 v = 0;
    const std::size_t start = pos;
    while (pos < s.size() && pos - start < 5 && s[pos] >= '0' && s[pos] <= '9') v = v * 10 + static_cast<u32>(s[pos++] - '0');
    if (pos == start || v > 0xFFFF) return false;
    out = static_cast<u16>(v);
    return true;
}

}  // namespace

std::optional<NinVersion> findNintendontVersion(FileSystem& fs, std::string_view dolPath) {
    std::vector<u8> buf(kScanBlock);
    for (u64 offset = 0;; offset += kScanBlock - kScanOverlap) {
        auto got = fs.read(dolPath, offset, buf);
        if (!got.isOk() || got.value() == 0) return std::nullopt;
        const std::string_view block(reinterpret_cast<const char*>(buf.data()), got.value());
        const std::size_t at = block.find(kVersionTag);
        if (at != std::string_view::npos) {
            // Die Zahl kann hinter dem Block liegen: gezielt nachlesen.
            std::array<u8, kNumberBytes> num{};
            auto n = fs.read(dolPath, offset + at + kVersionTag.size(), num);
            if (!n.isOk()) return std::nullopt;
            const std::string_view s(reinterpret_cast<const char*>(num.data()), n.value());
            std::size_t pos = 0;
            NinVersion v;
            if (!readNumber(s, pos, v.major) || pos >= s.size() || s[pos++] != '.' || !readNumber(s, pos, v.minor))
                return std::nullopt;
            return v;
        }
        if (got.value() < kScanBlock) return std::nullopt;
    }
}

std::string formatNinVersion(NinVersion v) { return std::to_string(v.major) + "." + std::to_string(v.minor); }

}  // namespace nl
