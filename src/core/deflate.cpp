#include "core/deflate.hpp"

#include <algorithm>
#include <array>

namespace nl {
namespace {

constexpr std::size_t kWindow = 32768;
constexpr std::size_t kMinMatch = 3;
constexpr std::size_t kMaxMatch = 258;
constexpr int kChainDepth = 32;
constexpr std::size_t kSegment = 65535;  // höchste Länge eines gespeicherten Blocks
constexpr u32 kHashBits = 15;

constexpr std::array<u16, 29> kLenBase{3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                       31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<u8, 29> kLenExtra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<u16, 30> kDistBase{1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                        193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<u8, 30> kDistExtra{0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

class BitWriter {
public:
    explicit BitWriter(std::vector<u8>& out) : out_(out) {}
    void bits(u32 value, unsigned count) {  // LSB zuerst
        for (unsigned i = 0; i < count; ++i) {
            if (used_ == 0) out_.push_back(0);
            if ((value >> i) & 1u) out_.back() = static_cast<u8>(out_.back() | (1u << used_));
            used_ = (used_ + 1) & 7u;
        }
    }
    void huff(u32 code, unsigned len) {  // Huffman-Codes stehen MSB zuerst im Strom
        for (unsigned i = len; i-- > 0;) bits((code >> i) & 1u, 1);
    }
    void align() { used_ = 0; }
    std::size_t bitPos() const { return out_.size() * 8 - (used_ ? 8 - used_ : 0); }

private:
    std::vector<u8>& out_;
    unsigned used_ = 0;  // belegte Bits im letzten Byte, 0 = voll bzw. keins
};

struct FixedCode {
    u32 code;
    unsigned len;
};

FixedCode fixedLit(unsigned v) {
    if (v < 144) return {0x30u + v, 8};
    if (v < 256) return {0x190u + (v - 144), 9};
    if (v < 280) return {v - 256, 7};
    return {0xC0u + (v - 280), 8};
}

unsigned lenIndex(std::size_t len) {
    unsigned i = 28;
    while (kLenBase[i] > len) --i;
    return i;
}

unsigned distIndex(std::size_t dist) {
    unsigned i = 29;
    while (kDistBase[i] > dist) --i;
    return i;
}

struct Token {
    u16 len;   // 0 = Literal
    u16 dist;  // bei Literal: das Byte
};

std::size_t tokenBits(const Token& t) {
    if (t.len == 0) return fixedLit(t.dist).len;
    const unsigned li = lenIndex(t.len);
    return fixedLit(257 + li).len + kLenExtra[li] + 5 + kDistExtra[distIndex(t.dist)];
}

void writeToken(BitWriter& w, const Token& t) {
    if (t.len == 0) {
        const FixedCode c = fixedLit(t.dist);
        w.huff(c.code, c.len);
        return;
    }
    const unsigned li = lenIndex(t.len);
    const FixedCode c = fixedLit(257 + li);
    w.huff(c.code, c.len);
    w.bits(t.len - kLenBase[li], kLenExtra[li]);
    const unsigned di = distIndex(t.dist);
    w.huff(di, 5);
    w.bits(t.dist - kDistBase[di], kDistExtra[di]);
}

class Matcher {
public:
    explicit Matcher(std::span<const u8> in) : in_(in), head_(std::size_t{1} << kHashBits, -1), prev_(kWindow, -1) {}

    // Längster Treffer ab pos, höchstens bis limit (exklusiv); fügt pos in die Ketten ein.
    Token find(std::size_t pos, std::size_t limit) {
        Token best{0, in_[pos]};
        if (pos + kMinMatch <= in_.size()) {
            const std::size_t maxLen = std::min(kMaxMatch, limit - pos);
            int cand = head_[hash(pos)];
            for (int depth = 0; cand >= 0 && depth < kChainDepth; ++depth) {
                const std::size_t c = static_cast<std::size_t>(cand);
                if (pos - c > kWindow) break;
                std::size_t n = 0;
                while (n < maxLen && in_[c + n] == in_[pos + n]) ++n;
                if (n >= kMinMatch && n > best.len) {
                    best = Token{static_cast<u16>(n), static_cast<u16>(pos - c)};
                    if (n == maxLen) break;
                }
                cand = prev_[c % kWindow];
            }
        }
        insert(pos);
        return best;
    }
    void insert(std::size_t pos) {
        if (pos + kMinMatch > in_.size()) return;
        const u32 h = hash(pos);
        prev_[pos % kWindow] = head_[h];
        head_[h] = static_cast<int>(pos);
    }

private:
    u32 hash(std::size_t p) const {
        const u32 v = (u32{in_[p]} << 16) | (u32{in_[p + 1]} << 8) | in_[p + 2];
        return (v * 2654435761u) >> (32 - kHashBits);
    }
    std::span<const u8> in_;
    std::vector<int> head_;
    std::vector<int> prev_;
};

}  // namespace

std::vector<u8> deflateRaw(std::span<const u8> in) {
    std::vector<u8> out;
    out.reserve(in.size() / 2 + 16);
    BitWriter w(out);
    if (in.empty()) {
        w.bits(1, 1);  // BFINAL
        w.bits(1, 2);  // feste Codes
        w.huff(fixedLit(256).code, fixedLit(256).len);
        return out;
    }
    Matcher m(in);
    std::vector<Token> tokens;
    for (std::size_t start = 0; start < in.size(); start += kSegment) {
        const std::size_t end = std::min(in.size(), start + kSegment);
        const bool last = end == in.size();
        tokens.clear();
        std::size_t fixedBits = 3 + fixedLit(256).len;
        for (std::size_t pos = start; pos < end;) {
            const Token t = m.find(pos, end);
            tokens.push_back(t);
            fixedBits += tokenBits(t);
            const std::size_t step = t.len ? t.len : 1;
            for (std::size_t k = 1; k < step; ++k) m.insert(pos + k);
            pos += step;
        }
        const std::size_t pos = w.bitPos();
        const std::size_t pad = (8 - (pos + 3) % 8) % 8;
        const std::size_t storedBits = 3 + pad + 32 + 8 * (end - start);
        w.bits(last ? 1u : 0u, 1);
        if (fixedBits < storedBits) {
            w.bits(1, 2);
            for (const Token& t : tokens) writeToken(w, t);
            w.huff(fixedLit(256).code, fixedLit(256).len);
        } else {
            w.bits(0, 2);
            w.align();
            const u16 len = static_cast<u16>(end - start);
            out.push_back(static_cast<u8>(len));
            out.push_back(static_cast<u8>(len >> 8));
            out.push_back(static_cast<u8>(~len));
            out.push_back(static_cast<u8>(~len >> 8));
            out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(start), in.begin() + static_cast<std::ptrdiff_t>(end));
        }
    }
    return out;
}

}  // namespace nl
