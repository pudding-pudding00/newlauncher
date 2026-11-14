#pragma once
// Baut TPL-Dateien für die Host-Tests (M18) aus eigenen Testmustern.

#include <functional>
#include <vector>

#include "core/cmpr.hpp"
#include "core/tpl.hpp"
#include "support/disc_images.hpp"

namespace nl::test {

struct Rgba {
    u8 r, g, b, a;
};

inline u16 toRgb565(Rgba c) { return static_cast<u16>((c.r >> 3) << 11 | (c.g >> 2) << 5 | (c.b >> 3)); }
inline u16 toRgb5a3(Rgba c) {
    if (c.a == 255) return static_cast<u16>(0x8000 | (c.r >> 3) << 10 | (c.g >> 3) << 5 | (c.b >> 3));
    return static_cast<u16>((c.a >> 5) << 12 | (c.r >> 4) << 8 | (c.g >> 4) << 4 | (c.b >> 4));
}
inline u16 toIa8(Rgba c) { return static_cast<u16>(c.a << 8 | c.r); }

struct TplSpec {
    TexFmt fmt;
    u16 w, h;
    std::function<Rgba(int, int)> pixel;      // direkte Formate
    std::function<u16(int, int)> index;       // C4/C8/C14X2
    TexFmt palFmt = TexFmt::RGB5A3;
    std::vector<u16> palette;                 // schon im Palettenformat
};

namespace detail {
inline void tileShape(TexFmt f, int& bw, int& bh, int& bits) {
    switch (f) {
        case TexFmt::I4: case TexFmt::C4: case TexFmt::CMPR: bw = 8; bh = 8; bits = 4; break;
        case TexFmt::I8: case TexFmt::IA4: case TexFmt::C8: bw = 8; bh = 4; bits = 8; break;
        default: bw = 4; bh = 4; bits = 16; break;
    }
}
inline u32 texelValue(const TplSpec& s, int x, int y) {
    if (x >= s.w || y >= s.h) return 0;
    if (s.fmt == TexFmt::C4 || s.fmt == TexFmt::C8 || s.fmt == TexFmt::C14X2) return s.index(x, y);
    const Rgba c = s.pixel(x, y);
    switch (s.fmt) {
        case TexFmt::I4: return c.r >> 4;
        case TexFmt::I8: return c.r;
        case TexFmt::IA4: return (c.a >> 4) << 4 | (c.r >> 4);
        case TexFmt::IA8: return toIa8(c);
        case TexFmt::RGB565: return toRgb565(c);
        case TexFmt::RGB5A3: return toRgb5a3(c);
        default: return 0;
    }
}
}  // namespace detail

inline std::vector<u8> encodeTex(const TplSpec& s) {
    std::vector<u8> out;
    if (s.fmt == TexFmt::CMPR) {
        std::vector<u8> rgba(std::size_t{s.w} * s.h * 4);
        for (int y = 0; y < s.h; ++y)
            for (int x = 0; x < s.w; ++x) {
                const Rgba c = s.pixel(x, y);
                u8* p = &rgba[(std::size_t(y) * s.w + x) * 4];
                p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a;
            }
        return encodeCmpr(rgba, s.w, s.h);
    }
    if (s.fmt == TexFmt::RGBA8) {
        for (int ty = 0; ty < s.h; ty += 4)
            for (int tx = 0; tx < s.w; tx += 4) {
                std::vector<u8> ar, gb;
                for (int y = ty; y < ty + 4; ++y)
                    for (int x = tx; x < tx + 4; ++x) {
                        const Rgba c = (x < s.w && y < s.h) ? s.pixel(x, y) : Rgba{0, 0, 0, 0};
                        ar.push_back(c.a); ar.push_back(c.r);
                        gb.push_back(c.g); gb.push_back(c.b);
                    }
                out.insert(out.end(), ar.begin(), ar.end());
                out.insert(out.end(), gb.begin(), gb.end());
            }
        return out;
    }
    int bw = 0, bh = 0, bits = 0;
    detail::tileShape(s.fmt, bw, bh, bits);
    for (int ty = 0; ty < s.h; ty += bh)
        for (int tx = 0; tx < s.w; tx += bw)
            for (int y = ty; y < ty + bh; ++y)
                for (int x = tx; x < tx + bw; ++x) {
                    const u32 v = detail::texelValue(s, x, y);
                    if (bits == 4) {
                        if ((x - tx) % 2 == 0) out.push_back(static_cast<u8>(v << 4));
                        else out.back() = static_cast<u8>(out.back() | v);
                    } else if (bits == 8) {
                        out.push_back(static_cast<u8>(v));
                    } else {
                        out.push_back(static_cast<u8>(v >> 8));
                        out.push_back(static_cast<u8>(v));
                    }
                }
    return out;
}

inline void putBe16(std::vector<u8>& b, std::size_t at, u16 v) {
    b[at] = static_cast<u8>(v >> 8);
    b[at + 1] = static_cast<u8>(v);
}

// TPL: Kopf 0x0C, Bildtabelle, je Bild 0x24-Kopf (+ 0x0C-Palettenkopf), Daten 32-Byte-ausgerichtet.
inline std::vector<u8> tplFile(const std::vector<TplSpec>& images) {
    std::vector<u8> out(0x0C + images.size() * 8, 0);
    putBe32(out, 0, 0x0020AF30u);
    putBe32(out, 4, static_cast<u32>(images.size()));
    putBe32(out, 8, 0x0C);
    auto align = [&] { out.resize((out.size() + 31) & ~std::size_t{31}, 0); };
    for (std::size_t i = 0; i < images.size(); ++i) {
        const TplSpec& s = images[i];
        const bool pal = s.fmt == TexFmt::C4 || s.fmt == TexFmt::C8 || s.fmt == TexFmt::C14X2;
        std::size_t palHeader = 0;
        if (pal) {
            align();
            palHeader = out.size();
            out.resize(out.size() + 0x0C, 0);
            const u32 tlut = s.palFmt == TexFmt::IA8 ? 0 : s.palFmt == TexFmt::RGB565 ? 1 : 2;
            putBe16(out, palHeader, static_cast<u16>(s.palette.size()));
            putBe32(out, palHeader + 4, tlut);
            align();
            putBe32(out, palHeader + 8, static_cast<u32>(out.size()));
            for (u16 v : s.palette) {
                out.push_back(static_cast<u8>(v >> 8));
                out.push_back(static_cast<u8>(v));
            }
        }
        align();
        const std::size_t header = out.size();
        out.resize(out.size() + 0x24, 0);
        putBe16(out, header, s.h);
        putBe16(out, header + 2, s.w);
        putBe32(out, header + 4, static_cast<u32>(s.fmt));
        align();
        putBe32(out, header + 8, static_cast<u32>(out.size()));
        const std::vector<u8> tex = encodeTex(s);
        out.insert(out.end(), tex.begin(), tex.end());
        putBe32(out, 0x0C + i * 8, static_cast<u32>(header));
        putBe32(out, 0x0C + i * 8 + 4, static_cast<u32>(palHeader));
    }
    return out;
}

}  // namespace nl::test
