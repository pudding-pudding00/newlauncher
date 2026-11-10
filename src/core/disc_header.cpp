#include "core/disc_header.hpp"

namespace nl {

namespace {

constexpr std::size_t kHeaderBytes = 0x100;
constexpr std::size_t kTitleOffset = 0x20, kTitleMax = 64;
constexpr u32 kWiiMagic = 0x5D1C9EA3, kGcMagic = 0xC2339F3D;

u32 be32(std::span<const u8> b, std::size_t at) {
    return static_cast<u32>(b[at]) << 24 | static_cast<u32>(b[at + 1]) << 16 | static_cast<u32>(b[at + 2]) << 8 | b[at + 3];
}

std::optional<DiscInfo> parseHeader(std::span<const u8> h, std::size_t magicAt, u32 magic) {
    if (h.size() < kHeaderBytes || be32(h, magicAt) != magic) return std::nullopt;
    DiscInfo info{};
    for (int i = 0; i < 6; ++i) {
        const u8 c = h[static_cast<std::size_t>(i)];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return std::nullopt;
        info.id6[i] = static_cast<char>(c);
    }
    info.id6[6] = '\0';
    for (std::size_t i = 0; i < kTitleMax; ++i) {
        const u8 c = h[kTitleOffset + i];
        if (c == 0) break;
        if (c < 0x80) {
            info.title += static_cast<char>(c < 0x20 ? ' ' : c);
        } else {  // Latin-1 → UTF-8
            info.title += static_cast<char>(0xC0 | (c >> 6));
            info.title += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return info;
}

}  // namespace

std::optional<DiscInfo> parseWiiIso(std::span<const u8> h) { return parseHeader(h, 0x18, kWiiMagic); }

std::optional<DiscInfo> parseGcIso(std::span<const u8> h) { return parseHeader(h, 0x1C, kGcMagic); }

std::optional<DiscInfo> parseWbfs(std::span<const u8> f) {
    if (f.size() < 16 || f[0] != 'W' || f[1] != 'B' || f[2] != 'F' || f[3] != 'S') return std::nullopt;
    const u8 shift = f[8];
    if (shift < 9 || shift > 9) return std::nullopt;  // nur 512-Byte-Sektoren (FAT32-Standard)
    const std::size_t base = std::size_t{1} << shift;
    if (f.size() < base + kHeaderBytes) return std::nullopt;
    return parseWiiIso(f.subspan(base, kHeaderBytes));
}

}  // namespace nl
