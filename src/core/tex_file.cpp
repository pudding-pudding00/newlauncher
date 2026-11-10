#include "core/tex_file.hpp"

#include <algorithm>

namespace nl {
namespace {
void put16(u8* p, u16 v) { p[0] = u8(v >> 8), p[1] = u8(v); }
void put32(u8* p, u32 v) { p[0] = u8(v >> 24), p[1] = u8(v >> 16), p[2] = u8(v >> 8), p[3] = u8(v); }
u16 get16(const u8* p) { return u16(p[0] << 8 | p[1]); }
u32 get32(const u8* p) { return u32(p[0]) << 24 | u32(p[1]) << 16 | u32(p[2]) << 8 | p[3]; }
}  // namespace

std::vector<u8> writeTex(u16 w, u16 h, std::span<const u8> cmpr) {
    std::vector<u8> out(kTexHeaderBytes + cmpr.size());
    out[0] = 'N', out[1] = 'L', out[2] = 'T', out[3] = 'X';
    put16(&out[4], w);
    put16(&out[6], h);
    put32(&out[8], kGxFormatCmpr);
    put32(&out[12], u32(cmpr.size()));
    std::copy(cmpr.begin(), cmpr.end(), out.begin() + kTexHeaderBytes);
    return out;
}

Result<TexView> parseTex(std::span<const u8> file) {
    auto bad = [](const char* why) { return Result<TexView>::err({ErrorCode::InvalidData, why}); };
    if (file.size() < kTexHeaderBytes) return bad("tex: zu kurz");
    const u8* p = file.data();
    if (p[0] != 'N' || p[1] != 'L' || p[2] != 'T' || p[3] != 'X') return bad("tex: magic");
    const u16 w = get16(p + 4), h = get16(p + 6);
    if (get32(p + 8) != kGxFormatCmpr) return bad("tex: format");
    const u32 len = get32(p + 12);
    if (w == 0 || h == 0 || w % 8 || h % 8 || len != u32(w) * h / 2) return bad("tex: größe");
    if (file.size() - kTexHeaderBytes < len) return bad("tex: abgeschnitten");
    return Result<TexView>::ok({w, h, file.subspan(kTexHeaderBytes, len)});
}

}  // namespace nl
