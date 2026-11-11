#include "core/nin_cfg.hpp"

#include <algorithm>

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

}  // namespace nl
