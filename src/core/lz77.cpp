#include "core/lz77.hpp"

namespace nl {

namespace {

constexpr u8 kType10 = 0x10;
constexpr u8 kType11 = 0x11;

bool hasMagic(std::span<const u8> in) {
    return in.size() >= 5 && in[0] == 'L' && in[1] == 'Z' && in[2] == '7' && in[3] == '7';
}

bool isType(u8 b) { return b == kType10 || b == kType11; }

Result<std::vector<u8>> invalid(const char* what) {
    return Result<std::vector<u8>>::err({ErrorCode::InvalidData, what});
}

u32 readLe(std::span<const u8> in, std::size_t pos, std::size_t n) {
    u32 v = 0;
    for (std::size_t i = 0; i < n; ++i) v |= u32(in[pos + i]) << (8 * i);
    return v;
}

}  // namespace

bool isLz77(std::span<const u8> in) {
    if (in.size() < 4) return false;
    if (hasMagic(in)) return isType(in[4]);
    return isType(in[0]);
}

Result<std::vector<u8>> lz77Decompress(std::span<const u8> in, u32 maxOut) {
    std::size_t pos = hasMagic(in) ? 4 : 0;
    if (in.size() < pos + 4) return invalid("lz77: Kopf zu kurz");
    const u8 type = in[pos];
    if (!isType(type)) return invalid("lz77: unbekannter Typ");
    u32 size = readLe(in, pos + 1, 3);
    pos += 4;
    if (size == 0) {
        if (in.size() < pos + 4) return invalid("lz77: erweiterter Kopf zu kurz");
        size = readLe(in, pos, 4);
        pos += 4;
    }
    if (size == 0 || size > maxOut) return invalid("lz77: Größe ungültig");

    std::vector<u8> out;
    out.reserve(size);
    while (out.size() < size) {
        if (pos >= in.size()) return invalid("lz77: Eingabe endet zu früh");
        const u8 flags = in[pos++];
        for (int bit = 0; bit < 8 && out.size() < size; ++bit) {
            if ((flags & (0x80 >> bit)) == 0) {
                if (pos >= in.size()) return invalid("lz77: Eingabe endet zu früh");
                out.push_back(in[pos++]);
                continue;
            }
            if (pos + 2 > in.size()) return invalid("lz77: Eingabe endet zu früh");
            const u8 b0 = in[pos];
            const u8 b1 = in[pos + 1];
            u32 length = 0;
            u32 dist = 0;
            if (type == kType10) {
                length = u32(b0 >> 4) + 3;
                dist = ((u32(b0 & 0xF) << 8) | b1) + 1;
                pos += 2;
            } else {
                const u32 ind = b0 >> 4;
                if (ind == 0) {
                    if (pos + 3 > in.size()) return invalid("lz77: Eingabe endet zu früh");
                    const u8 b2 = in[pos + 2];
                    length = ((u32(b0 & 0xF) << 4) | (b1 >> 4)) + 0x11;
                    dist = ((u32(b1 & 0xF) << 8) | b2) + 1;
                    pos += 3;
                } else if (ind == 1) {
                    if (pos + 4 > in.size()) return invalid("lz77: Eingabe endet zu früh");
                    const u8 b2 = in[pos + 2];
                    const u8 b3 = in[pos + 3];
                    length = ((u32(b0 & 0xF) << 12) | (u32(b1) << 4) | (b2 >> 4)) + 0x111;
                    dist = ((u32(b2 & 0xF) << 8) | b3) + 1;
                    pos += 4;
                } else {
                    length = ind + 1;
                    dist = ((u32(b0 & 0xF) << 8) | b1) + 1;
                    pos += 2;
                }
            }
            if (dist > out.size()) return invalid("lz77: Rückverweis vor den Anfang");
            const std::size_t room = size - out.size();
            const std::size_t n = length < room ? length : room;
            const std::size_t from = out.size() - dist;
            for (std::size_t i = 0; i < n; ++i) out.push_back(out[from + i]);  // überlappend erlaubt
        }
    }
    return Result<std::vector<u8>>::ok(std::move(out));
}

}  // namespace nl
