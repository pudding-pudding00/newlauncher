#include "core/tpl.hpp"

namespace nl {

namespace {
u32 be32(std::span<const u8> b, std::size_t at) {
    return static_cast<u32>(b[at]) << 24 | static_cast<u32>(b[at + 1]) << 16 | static_cast<u32>(b[at + 2]) << 8 | b[at + 3];
}
u16 be16(std::span<const u8> b, std::size_t at) { return static_cast<u16>(b[at] << 8 | b[at + 1]); }
Result<std::vector<TplImage>> invalid(const char* what) {
    return Result<std::vector<TplImage>>::err(Error{ErrorCode::InvalidData, what});
}

struct Shape {
    u8 bw, bh;     // Kachel in Pixeln
    u8 tileBytes;  // 32, bei RGBA8 64
};
bool shapeOf(TexFmt f, Shape& s) {
    switch (f) {
        case TexFmt::I4: case TexFmt::C4: case TexFmt::CMPR: s = {8, 8, 32}; return true;
        case TexFmt::I8: case TexFmt::IA4: case TexFmt::C8: s = {8, 4, 32}; return true;
        case TexFmt::IA8: case TexFmt::RGB565: case TexFmt::RGB5A3: case TexFmt::C14X2: s = {4, 4, 32}; return true;
        case TexFmt::RGBA8: s = {4, 4, 64}; return true;
    }
    return false;
}
bool isPaletted(TexFmt f) { return f == TexFmt::C4 || f == TexFmt::C8 || f == TexFmt::C14X2; }

u8 expand3(u32 v) { return static_cast<u8>((v << 5) | (v << 2) | (v >> 1)); }
u8 expand4(u32 v) { return static_cast<u8>(v * 17); }
u8 expand5(u32 v) { return static_cast<u8>((v << 3) | (v >> 2)); }
u8 expand6(u32 v) { return static_cast<u8>((v << 2) | (v >> 4)); }

struct Px {
    u8 r = 0, g = 0, b = 0, a = 0;
};
Px fromRgb565(u16 v) { return {expand5(v >> 11), expand6((v >> 5) & 0x3F), expand5(v & 0x1F), 255}; }
Px fromRgb5a3(u16 v) {
    if (v & 0x8000) return {expand5((v >> 10) & 0x1F), expand5((v >> 5) & 0x1F), expand5(v & 0x1F), 255};
    return {expand4((v >> 8) & 0xF), expand4((v >> 4) & 0xF), expand4(v & 0xF), expand3((v >> 12) & 0x7)};
}
Px fromIa8(u16 v) {
    const u8 i = static_cast<u8>(v);
    return {i, i, i, static_cast<u8>(v >> 8)};
}
Px fromPalette(const TplImage& img, u32 index) {
    if (index >= img.palCount) return {};
    const u16 v = be16(img.palette, std::size_t{index} * 2);
    switch (img.palFmt) {
        case TexFmt::IA8: return fromIa8(v);
        case TexFmt::RGB565: return fromRgb565(v);
        default: return fromRgb5a3(v);
    }
}

u8 mix(u8 a, u8 c, int wa, int wc) { return static_cast<u8>((a * wa + c * wc) / (wa + wc)); }
Px mixPx(Px a, Px c, int wa, int wc) { return {mix(a.r, c.r, wa, wc), mix(a.g, c.g, wa, wc), mix(a.b, c.b, wa, wc), 255}; }

// Ein CMPR-Unterblock (4×4, 8 Byte): zwei RGB565-Farben, dann je Zeile ein Byte mit 2-Bit-Indizes.
void cmprBlock(std::span<const u8> b, Px out[16]) {
    const u16 c0 = be16(b, 0), c1 = be16(b, 2);
    Px pal[4] = {fromRgb565(c0), fromRgb565(c1), {}, {}};
    if (c0 > c1) {
        pal[2] = mixPx(pal[0], pal[1], 2, 1);
        pal[3] = mixPx(pal[0], pal[1], 1, 2);
    } else {
        pal[2] = mixPx(pal[0], pal[1], 1, 1);
        pal[3] = {0, 0, 0, 0};  // 3-Farben-Modus: Index 3 ist transparent
    }
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) out[y * 4 + x] = pal[(b[4 + y] >> (6 - x * 2)) & 3];
}
}  // namespace

u32 texBytes(TexFmt f, u16 w, u16 h) {
    Shape s{};
    if (!shapeOf(f, s)) return 0;
    const u32 tx = (u32{w} + s.bw - 1) / s.bw;
    const u32 ty = (u32{h} + s.bh - 1) / s.bh;
    return tx * ty * s.tileBytes;
}

Result<std::vector<TplImage>> parseTpl(std::span<const u8> file) {
    if (file.size() < 0x0C || be32(file, 0) != 0x0020AF30u) return invalid("TPL: kein TPL");
    const u32 count = be32(file, 4);
    const std::size_t table = be32(file, 8);
    if (count == 0 || count > kTplMaxImages) return invalid("TPL: Bildanzahl");
    if (table > file.size() || (file.size() - table) / 8 < count) return invalid("TPL: Bildtabelle");
    std::vector<TplImage> out;
    out.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        const std::size_t hdr = be32(file, table + std::size_t{i} * 8);
        const std::size_t palHdr = be32(file, table + std::size_t{i} * 8 + 4);
        if (hdr > file.size() || file.size() - hdr < 0x0C) return invalid("TPL: Bildkopf");
        TplImage img;
        img.h = be16(file, hdr);
        img.w = be16(file, hdr + 2);
        const u32 fmt = be32(file, hdr + 4);
        const std::size_t dataAt = be32(file, hdr + 8);
        if (img.w == 0 || img.h == 0 || img.w > kTplMaxSide || img.h > kTplMaxSide) return invalid("TPL: Größe");
        Shape s{};
        if (fmt > 0xFF || !shapeOf(static_cast<TexFmt>(fmt), s)) return invalid("TPL: Format");
        img.fmt = static_cast<TexFmt>(fmt);
        const u32 bytes = texBytes(img.fmt, img.w, img.h);
        if (dataAt > file.size() || file.size() - dataAt < bytes) return invalid("TPL: Daten hinter dem Ende");
        img.data = file.subspan(dataAt, bytes);
        if (isPaletted(img.fmt)) {
            if (palHdr == 0 || palHdr > file.size() || file.size() - palHdr < 0x0C) return invalid("TPL: Palette fehlt");
            img.palCount = be16(file, palHdr);
            const u32 tlut = be32(file, palHdr + 4);
            const std::size_t palAt = be32(file, palHdr + 8);
            if (img.palCount == 0 || img.palCount > 16384) return invalid("TPL: Palettengröße");
            if (tlut > 2) return invalid("TPL: Palettenformat");
            img.palFmt = tlut == 0 ? TexFmt::IA8 : tlut == 1 ? TexFmt::RGB565 : TexFmt::RGB5A3;
            const std::size_t palBytes = std::size_t{img.palCount} * 2;
            if (palAt > file.size() || file.size() - palAt < palBytes) return invalid("TPL: Palette hinter dem Ende");
            img.palette = file.subspan(palAt, palBytes);
        }
        out.push_back(img);
    }
    return Result<std::vector<TplImage>>::ok(std::move(out));
}

std::vector<u8> decodeToRgba(const TplImage& img) {
    Shape s{};
    if (!shapeOf(img.fmt, s) || img.data.size() < texBytes(img.fmt, img.w, img.h)) return {};
    std::vector<u8> out(std::size_t{img.w} * img.h * 4, 0);
    auto put = [&](u32 x, u32 y, Px p) {
        if (x >= img.w || y >= img.h) return;
        u8* d = &out[(std::size_t{y} * img.w + x) * 4];
        d[0] = p.r;
        d[1] = p.g;
        d[2] = p.b;
        d[3] = p.a;
    };
    std::size_t tile = 0;
    for (u32 ty = 0; ty < img.h; ty += s.bh)
        for (u32 tx = 0; tx < img.w; tx += s.bw, tile += s.tileBytes) {
            const std::span<const u8> t = img.data.subspan(tile, s.tileBytes);
            if (img.fmt == TexFmt::CMPR) {
                for (u32 sb = 0; sb < 4; ++sb) {
                    Px px[16];
                    cmprBlock(t.subspan(sb * 8, 8), px);
                    const u32 ox = tx + (sb & 1) * 4, oy = ty + (sb >> 1) * 4;
                    for (u32 k = 0; k < 16; ++k) put(ox + k % 4, oy + k / 4, px[k]);
                }
                continue;
            }
            for (u32 k = 0; k < u32{s.bw} * s.bh; ++k) {
                Px p;
                switch (img.fmt) {
                    case TexFmt::I4:
                    case TexFmt::C4: {
                        const u32 n = (t[k / 2] >> ((k & 1) ? 0 : 4)) & 0xF;
                        p = img.fmt == TexFmt::C4 ? fromPalette(img, n) : Px{expand4(n), expand4(n), expand4(n), expand4(n)};
                        break;
                    }
                    case TexFmt::I8: p = {t[k], t[k], t[k], t[k]}; break;
                    case TexFmt::C8: p = fromPalette(img, t[k]); break;
                    case TexFmt::IA4: {
                        const u8 i = expand4(t[k] & 0xF);
                        p = {i, i, i, expand4(t[k] >> 4)};
                        break;
                    }
                    case TexFmt::IA8: p = fromIa8(be16(t, k * 2)); break;
                    case TexFmt::RGB565: p = fromRgb565(be16(t, k * 2)); break;
                    case TexFmt::RGB5A3: p = fromRgb5a3(be16(t, k * 2)); break;
                    case TexFmt::C14X2: p = fromPalette(img, be16(t, k * 2) & 0x3FFFu); break;
                    case TexFmt::RGBA8: p = {t[k * 2 + 1], t[32 + k * 2], t[32 + k * 2 + 1], t[k * 2]}; break;
                    case TexFmt::CMPR: break;
                }
                put(tx + k % s.bw, ty + k / s.bw, p);
            }
        }
    return out;
}

std::vector<u8> toRgb5a3Tiles(const TplImage& img) {
    if (!isPaletted(img.fmt)) return {};
    const std::vector<u8> rgba = decodeToRgba(img);
    if (rgba.empty()) return {};
    std::vector<u8> out(texBytes(TexFmt::RGB5A3, img.w, img.h), 0);
    std::size_t at = 0;
    for (u32 ty = 0; ty < img.h; ty += 4)
        for (u32 tx = 0; tx < img.w; tx += 4)
            for (u32 k = 0; k < 16; ++k, at += 2) {
                const u32 x = tx + k % 4, y = ty + k / 4;
                if (x >= img.w || y >= img.h) continue;  // Kachelrand bleibt 0
                const u8* p = &rgba[(std::size_t{y} * img.w + x) * 4];
                const u16 v = p[3] == 255 ? static_cast<u16>(0x8000 | (p[0] >> 3) << 10 | (p[1] >> 3) << 5 | (p[2] >> 3))
                                          : static_cast<u16>((p[3] >> 5) << 12 | (p[0] >> 4) << 8 | (p[1] >> 4) << 4 | (p[2] >> 4));
                out[at] = static_cast<u8>(v >> 8);
                out[at + 1] = static_cast<u8>(v);
            }
    return out;
}

}  // namespace nl
