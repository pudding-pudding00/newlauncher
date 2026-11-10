#include "core/cover_image.hpp"

#include <algorithm>

#include "core/tex_file.hpp"

namespace nl {

std::vector<u8> fitCover(std::span<const u8> rgba, u32 w, u32 h) {
    if (w == 0 || h == 0 || rgba.size() / 4 / w < h) return {};
    u32 dw = kCoverW, dh = kCoverH;
    if (u64(w) * kCoverH >= u64(h) * kCoverW) dh = u32((u64(h) * kCoverW + w / 2) / w);
    else dw = u32((u64(w) * kCoverH + h / 2) / h);
    dw = std::clamp<u32>(dw, 1, kCoverW);
    dh = std::clamp<u32>(dh, 1, kCoverH);
    const u32 ox = (kCoverW - dw) / 2, oy = (kCoverH - dh) / 2;

    std::vector<u8> out(std::size_t(kCoverW) * kCoverH * 4, 0);
    for (u32 y = 0; y < dh; ++y) {
        const u32 sy0 = u32(u64(y) * h / dh);
        const u32 sy1 = std::max(sy0 + 1, u32(u64(y + 1) * h / dh));
        for (u32 x = 0; x < dw; ++x) {
            const u32 sx0 = u32(u64(x) * w / dw);
            const u32 sx1 = std::max(sx0 + 1, u32(u64(x + 1) * w / dw));
            u64 r = 0, g = 0, b = 0, a = 0, n = 0;
            for (u32 sy = sy0; sy < sy1; ++sy) {
                const u8* p = &rgba[(std::size_t(sy) * w + sx0) * 4];
                for (u32 sx = sx0; sx < sx1; ++sx, p += 4) {
                    r += u64(p[0]) * p[3], g += u64(p[1]) * p[3], b += u64(p[2]) * p[3], a += p[3];
                    ++n;
                }
            }
            u8* d = &out[(std::size_t(oy + y) * kCoverW + ox + x) * 4];
            if (a > 0) d[0] = u8(r / a), d[1] = u8(g / a), d[2] = u8(b / a);
            d[3] = u8(a / n);
        }
    }
    return out;
}

}  // namespace nl
