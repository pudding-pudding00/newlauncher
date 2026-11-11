#include "core/ios_info.hpp"

namespace nl {
namespace {

u32 be32(const u8* p) { return (u32{p[0]} << 24) | (u32{p[1]} << 16) | (u32{p[2]} << 8) | u32{p[3]}; }

// Feste 16-Byte-Felder: bis zum ersten NUL, höchstens 16 Zeichen.
std::string field(const u8* p) {
    std::size_t n = 0;
    while (n < 16 && p[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(p), n);
}

}  // namespace

std::optional<IosInfo> parseIosInfo(std::span<const u8> b) {
    if (b.size() < kIosInfoBytes) return std::nullopt;
    if (be32(&b[0]) != kIosInfoMagic || be32(&b[4]) != 1) return std::nullopt;
    IosInfo i;
    i.version = be32(&b[8]);
    i.baseios = be32(&b[12]);
    i.name = field(&b[16]);
    i.versionString = field(&b[32]);
    return i;
}

CiosStatus classifyCios(bool tmdPresent, const std::optional<IosInfo>& info) {
    if (!tmdPresent) return {CiosState::Missing, kDefaultCiosBase, ""};
    if (!info) return {CiosState::Unverified, kDefaultCiosBase, ""};
    if (info->name.rfind("d2x", 0) == 0) return {CiosState::D2x, info->baseios, info->versionString};
    return {CiosState::NotD2x, info->baseios, info->name};
}

}  // namespace nl
