#include "core/rgb5a3.hpp"

#include <cstring>

namespace nl {

namespace {
u16 texel(const u8* p) {
    if (p[3] == 0xFF) return static_cast<u16>(0x8000 | ((p[0] >> 3) << 10) | ((p[1] >> 3) << 5) | (p[2] >> 3));
    return static_cast<u16>(((p[3] >> 5) << 12) | ((p[0] >> 4) << 8) | ((p[1] >> 4) << 4) | (p[2] >> 4));
}

u32 be32(const u8* p) { return (u32{p[0]} << 24) | (u32{p[1]} << 16) | (u32{p[2]} << 8) | p[3]; }
}  // namespace

Rgb5a3Image encodeRgb5a3(std::span<const u8> rgba, u16 width, u16 height) {
    Rgb5a3Image img;
    if (width == 0 || height == 0 || rgba.size() < std::size_t{width} * height * 4) return img;
    img.width = static_cast<u16>((width + 3) & ~3);
    img.height = static_cast<u16>((height + 3) & ~3);
    img.texels.assign(std::size_t{img.width} * img.height * 2, 0);
    std::size_t o = 0;
    for (u32 ty = 0; ty < img.height; ty += 4)
        for (u32 tx = 0; tx < img.width; tx += 4)
            for (u32 y = ty; y < ty + 4; ++y)
                for (u32 x = tx; x < tx + 4; ++x, o += 2) {
                    if (x >= width || y >= height) continue;
                    const u16 v = texel(rgba.data() + (std::size_t{y} * width + x) * 4);
                    img.texels[o] = static_cast<u8>(v >> 8);
                    img.texels[o + 1] = static_cast<u8>(v);
                }
    return img;
}

std::optional<std::pair<u32, u32>> pngSize(std::span<const u8> file) {
    static constexpr u8 kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (file.size() < 24 || std::memcmp(file.data(), kSig, 8) != 0 || std::memcmp(file.data() + 12, "IHDR", 4) != 0)
        return std::nullopt;
    return std::pair<u32, u32>{be32(file.data() + 16), be32(file.data() + 20)};
}

bool backgroundFits(u32 width, u32 height) {
    return width > 0 && height > 0 && width <= kMaxBackgroundWidth && height <= kMaxBackgroundHeight;
}

}  // namespace nl
