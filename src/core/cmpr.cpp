#include "core/cmpr.hpp"

#include <utility>

namespace nl {
namespace {

struct Rgb {
    int r, g, b;
};

u16 toRgb565(const Rgb& c) { return u16(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3)); }

Rgb fromRgb565(u16 v) {
    const int r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    return {(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)};
}

int dist2(const Rgb& a, const Rgb& b) {
    const int dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
    return dr * dr + dg * dg + db * db;
}

void encodeBlock(std::span<const u8> rgba, u32 width, u32 x0, u32 y0, u8* out) {
    Rgb px[16];
    bool opaque[16];
    bool anyTransparent = false;
    int lo = -1, hi = -1, loLum = 1 << 30, hiLum = -1;
    for (u32 i = 0; i < 16; ++i) {
        const std::size_t at = (std::size_t(y0 + i / 4) * width + x0 + i % 4) * 4;
        px[i] = {rgba[at], rgba[at + 1], rgba[at + 2]};
        opaque[i] = rgba[at + 3] >= 128;
        if (!opaque[i]) {
            anyTransparent = true;
            continue;
        }
        const int lum = 299 * px[i].r + 587 * px[i].g + 114 * px[i].b;
        if (lum < loLum) loLum = lum, lo = int(i);
        if (lum > hiLum) hiLum = lum, hi = int(i);
    }

    u16 c0 = 0, c1 = 0;
    if (lo >= 0) {
        c0 = toRgb565(px[hi]);
        c1 = toRgb565(px[lo]);
    }
    Rgb pal[4];
    if (anyTransparent) {
        if (c0 > c1) std::swap(c0, c1);  // c0 <= c1: 3-Farben-Modus
        pal[0] = fromRgb565(c0);
        pal[1] = fromRgb565(c1);
        pal[2] = {(pal[0].r + pal[1].r) / 2, (pal[0].g + pal[1].g) / 2, (pal[0].b + pal[1].b) / 2};
    } else {
        if (c0 < c1) std::swap(c0, c1);
        // Gleiche Endpunkte: c0 > c1 erzwingen; eine der beiden bleibt die exakte Farbe.
        if (c0 == c1) {
            if (c0 == 0) c0 = 1;
            else c1 = u16(c0 - 1);
        }
        pal[0] = fromRgb565(c0);
        pal[1] = fromRgb565(c1);
        pal[2] = {(2 * pal[0].r + pal[1].r) / 3, (2 * pal[0].g + pal[1].g) / 3, (2 * pal[0].b + pal[1].b) / 3};
        pal[3] = {(pal[0].r + 2 * pal[1].r) / 3, (pal[0].g + 2 * pal[1].g) / 3, (pal[0].b + 2 * pal[1].b) / 3};
    }

    out[0] = u8(c0 >> 8);
    out[1] = u8(c0);
    out[2] = u8(c1 >> 8);
    out[3] = u8(c1);
    const int colors = anyTransparent ? 3 : 4;
    for (u32 y = 0; y < 4; ++y) {
        u8 row = 0;
        for (u32 x = 0; x < 4; ++x) {
            const u32 i = y * 4 + x;
            int best = 3;
            if (opaque[i]) {
                int bestD = 1 << 30;
                best = 0;
                for (int k = 0; k < colors; ++k) {
                    const int d = dist2(px[i], pal[k]);
                    if (d < bestD) bestD = d, best = k;
                }
            }
            row = u8(row | (best << (6 - 2 * x)));
        }
        out[4 + y] = row;
    }
}

}  // namespace

std::vector<u8> encodeCmpr(std::span<const u8> rgba, u16 width, u16 height) {
    if (width == 0 || height == 0 || width % 8 != 0 || height % 8 != 0) return {};
    if (rgba.size() < std::size_t(width) * height * 4) return {};
    std::vector<u8> out(std::size_t(width) * height / 2);
    u8* dst = out.data();
    for (u32 ty = 0; ty < height; ty += 8)
        for (u32 tx = 0; tx < width; tx += 8)
            for (u32 sub = 0; sub < 4; ++sub) {
                encodeBlock(rgba, width, tx + (sub & 1) * 4, ty + (sub >> 1) * 4, dst);
                dst += 8;
            }
    return out;
}

}  // namespace nl
