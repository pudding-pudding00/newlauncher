#include "core/gc_banner.hpp"

#include <cstring>

namespace nl {

namespace {
constexpr std::size_t kImage = 0x20;
constexpr std::size_t kTexts = 0x1820;
constexpr std::size_t kBlock = 0x140;

void appendUtf8(std::string& s, u8 c) {
    if (c < 0x80) {
        s += static_cast<char>(c);
    } else {
        s += static_cast<char>(0xC0 | (c >> 6));
        s += static_cast<char>(0x80 | (c & 0x3F));
    }
}

// Feld bis zum ersten Nullbyte, höchstens cap Bytes.
std::string text(std::span<const u8> b, std::size_t at, std::size_t cap, bool shiftJis) {
    std::string s;
    for (std::size_t i = 0; i < cap; ++i) {
        const u8 c = b[at + i];
        if (c == 0) break;
        if (shiftJis) {
            const bool lead = (c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC);
            if (lead) ++i;  // Folgebyte gehört dazu
            s += c < 0x80 ? static_cast<char>(c) : '?';
        } else {
            appendUtf8(s, c);
        }
    }
    return s;
}

u8 expand5(u32 v) { return static_cast<u8>((v << 3) | (v >> 2)); }
u8 expand4(u32 v) { return static_cast<u8>(v * 17); }
u8 expand3(u32 v) { return static_cast<u8>((v << 5) | (v << 2) | (v >> 1)); }
}  // namespace

Result<GcBanner> parseGcBanner(std::span<const u8> bnr, u8 langIndex, bool shiftJis) {
    auto invalid = [](const char* what) { return Result<GcBanner>::err(Error{ErrorCode::InvalidData, what}); };
    if (bnr.size() < 4) return invalid("BNR zu kurz");
    std::size_t blocks = 0;
    if (std::memcmp(bnr.data(), "BNR1", 4) == 0) blocks = 1;
    else if (std::memcmp(bnr.data(), "BNR2", 4) == 0) blocks = 6;
    else return invalid("BNR: Magic");
    if (bnr.size() < kTexts + blocks * kBlock) return invalid("BNR zu kurz");
    const std::size_t block = blocks == 1 || langIndex >= blocks ? 0 : langIndex;
    const std::size_t base = kTexts + block * kBlock;
    GcBanner out;
    std::memcpy(out.rgb5a3.data(), bnr.data() + kImage, out.rgb5a3.size());
    out.shortTitle = text(bnr, base, 0x20, shiftJis);
    out.title = text(bnr, base + 0x40, 0x40, shiftJis);
    out.maker = text(bnr, base + 0x80, 0x40, shiftJis);
    out.description = text(bnr, base + 0xC0, 0x80, shiftJis);
    if (out.title.empty()) out.title = out.shortTitle;
    if (out.maker.empty()) out.maker = text(bnr, base + 0x20, 0x20, shiftJis);
    return Result<GcBanner>::ok(std::move(out));
}

std::vector<u8> rgb5a3ToRgba(std::span<const u8> tiled, u16 w, u16 h) {
    if (w % 4 != 0 || h % 4 != 0 || tiled.size() < std::size_t{w} * h * 2) return {};
    std::vector<u8> out(std::size_t{w} * h * 4);
    std::size_t in = 0;
    for (u16 ty = 0; ty < h; ty += 4)
        for (u16 tx = 0; tx < w; tx += 4)
            for (u16 y = 0; y < 4; ++y)
                for (u16 x = 0; x < 4; ++x, in += 2) {
                    const u32 v = static_cast<u32>(tiled[in]) << 8 | tiled[in + 1];
                    u8* p = &out[((std::size_t{ty} + y) * w + tx + x) * 4];
                    if (v & 0x8000) {
                        p[0] = expand5((v >> 10) & 0x1F);
                        p[1] = expand5((v >> 5) & 0x1F);
                        p[2] = expand5(v & 0x1F);
                        p[3] = 255;
                    } else {
                        p[0] = expand4((v >> 8) & 0xF);
                        p[1] = expand4((v >> 4) & 0xF);
                        p[2] = expand4(v & 0xF);
                        p[3] = expand3((v >> 12) & 0x7);
                    }
                }
    return out;
}

}  // namespace nl
