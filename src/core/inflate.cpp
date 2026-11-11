#include "core/inflate.hpp"

#include <array>

namespace nl {

namespace {

constexpr int kMaxBits = 15;
constexpr int kMaxLitLen = 286, kMaxDist = 30, kFixedLitLen = 288;

// Kanonischer Huffman-Code: Anzahl Codes je Länge und Symbole nach Code sortiert.
struct Huffman {
    std::array<u16, kMaxBits + 1> count{};
    std::array<u16, kFixedLitLen> symbol{};
};

class Inflater {
public:
    Inflater(std::span<const u8> in, std::size_t outSize) : in_(in), outSize_(outSize) { out_.reserve(outSize); }

    bool run() {
        bool last = false;
        while (!last) {
            int bit = 0, type = 0;
            if (!bits(1, bit) || !bits(2, type)) return false;
            last = bit != 0;
            bool ok = false;
            if (type == 0) ok = stored();
            else if (type == 1) ok = fixed();
            else if (type == 2) ok = dynamic();
            if (!ok) return false;
        }
        return out_.size() == outSize_;
    }

    std::vector<u8> take() { return std::move(out_); }

private:
    bool bits(int need, int& value) {
        u32 v = bitBuf_;
        while (bitCnt_ < need) {
            if (pos_ >= in_.size()) return false;
            v |= static_cast<u32>(in_[pos_++]) << bitCnt_;
            bitCnt_ += 8;
        }
        bitBuf_ = v >> need;
        bitCnt_ -= need;
        value = static_cast<int>(v & ((1u << need) - 1u));
        return true;
    }

    bool put(u8 b) {
        if (out_.size() >= outSize_) return false;
        out_.push_back(b);
        return true;
    }

    bool stored() {
        bitBuf_ = 0;
        bitCnt_ = 0;  // auf Bytegrenze
        if (pos_ + 4 > in_.size()) return false;
        const u32 len = in_[pos_] | (in_[pos_ + 1] << 8);
        const u32 nlen = in_[pos_ + 2] | (in_[pos_ + 3] << 8);
        pos_ += 4;
        if (len != (~nlen & 0xFFFFu)) return false;
        if (pos_ + len > in_.size()) return false;
        for (u32 i = 0; i < len; ++i)
            if (!put(in_[pos_ + i])) return false;
        pos_ += len;
        return true;
    }

    // Bit für Bit dekodieren (Codes stehen MSB-zuerst im Strom).
    bool decode(const Huffman& h, int& sym) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            int b = 0;
            if (!bits(1, b)) return false;
            code |= b;
            const int count = h.count[len];
            if (code - count < first) {
                sym = h.symbol[index + (code - first)];
                return true;
            }
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return false;  // kein gültiger Code
    }

    // false bei überbelegtem Code; unvollständige Codes sind erlaubt (nur bei Einzelcodes sinnvoll, wie puff).
    static bool build(Huffman& h, const u16* length, int n) {
        h.count.fill(0);
        for (int s = 0; s < n; ++s) ++h.count[length[s]];
        if (h.count[0] == n) return true;  // keine Codes: erst ein Zugriff schlägt fehl
        int left = 1;
        for (int len = 1; len <= kMaxBits; ++len) {
            left <<= 1;
            left -= h.count[len];
            if (left < 0) return false;
        }
        std::array<u16, kMaxBits + 1> offs{};
        for (int len = 1; len < kMaxBits; ++len) offs[len + 1] = static_cast<u16>(offs[len] + h.count[len]);
        for (int s = 0; s < n; ++s)
            if (length[s] != 0) h.symbol[offs[length[s]]++] = static_cast<u16>(s);
        return true;
    }

    bool codes(const Huffman& lencode, const Huffman& distcode) {
        static constexpr u16 kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                             31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static constexpr u8 kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                             2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static constexpr u16 kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                              33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                              1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static constexpr u8 kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                              6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int sym = 0;
            if (!decode(lencode, sym)) return false;
            if (sym < 256) {
                if (!put(static_cast<u8>(sym))) return false;
            } else if (sym == 256) {
                return true;
            } else {
                sym -= 257;
                if (sym >= 29) return false;
                int extra = 0;
                if (!bits(kLenExtra[sym], extra)) return false;
                const int len = kLenBase[sym] + extra;
                int dsym = 0;
                if (!decode(distcode, dsym) || dsym >= 30) return false;
                if (!bits(kDistExtra[dsym], extra)) return false;
                const std::size_t dist = static_cast<std::size_t>(kDistBase[dsym] + extra);
                if (dist > out_.size()) return false;
                for (int i = 0; i < len; ++i)
                    if (!put(out_[out_.size() - dist])) return false;
            }
        }
    }

    bool fixed() {
        if (!fixedBuilt_) {
            u16 lengths[kFixedLitLen];
            int s = 0;
            for (; s < 144; ++s) lengths[s] = 8;
            for (; s < 256; ++s) lengths[s] = 9;
            for (; s < 280; ++s) lengths[s] = 7;
            for (; s < kFixedLitLen; ++s) lengths[s] = 8;
            build(fixedLen_, lengths, kFixedLitLen);
            for (s = 0; s < kMaxDist; ++s) lengths[s] = 5;
            build(fixedDist_, lengths, kMaxDist);
            fixedBuilt_ = true;
        }
        return codes(fixedLen_, fixedDist_);
    }

    bool dynamic() {
        static constexpr u8 kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        int nlen = 0, ndist = 0, ncode = 0;
        if (!bits(5, nlen) || !bits(5, ndist) || !bits(4, ncode)) return false;
        nlen += 257;
        ndist += 1;
        ncode += 4;
        if (nlen > kMaxLitLen || ndist > kMaxDist) return false;

        u16 lengths[kMaxLitLen + kMaxDist] = {};
        for (int i = 0; i < ncode; ++i) {
            int v = 0;
            if (!bits(3, v)) return false;
            lengths[kOrder[i]] = static_cast<u16>(v);
        }
        Huffman lencode, distcode;
        if (!build(lencode, lengths, 19)) return false;

        int index = 0;
        while (index < nlen + ndist) {
            int sym = 0;
            if (!decode(lencode, sym)) return false;
            if (sym < 16) {
                lengths[index++] = static_cast<u16>(sym);
                continue;
            }
            u16 len = 0;
            int rep = 0;
            if (sym == 16) {
                if (index == 0) return false;
                len = lengths[index - 1];
                if (!bits(2, rep)) return false;
                rep += 3;
            } else if (sym == 17) {
                if (!bits(3, rep)) return false;
                rep += 3;
            } else {
                if (!bits(7, rep)) return false;
                rep += 11;
            }
            if (index + rep > nlen + ndist) return false;
            while (rep-- > 0) lengths[index++] = len;
        }
        if (lengths[256] == 0) return false;  // ohne Blockende-Code nicht dekodierbar
        if (!build(lencode, lengths, nlen)) return false;
        if (!build(distcode, lengths + nlen, ndist)) return false;
        return codes(lencode, distcode);
    }

    std::span<const u8> in_;
    std::size_t pos_ = 0;
    u32 bitBuf_ = 0;
    int bitCnt_ = 0;
    std::size_t outSize_;
    std::vector<u8> out_;
    Huffman fixedLen_, fixedDist_;
    bool fixedBuilt_ = false;
};

}  // namespace

Result<std::vector<u8>> inflateRaw(std::span<const u8> in, std::size_t outSize) {
    using R = Result<std::vector<u8>>;
    Inflater inf(in, outSize);
    if (!inf.run()) return R::err(Error{ErrorCode::InvalidData, "Deflate-Daten ungültig"});
    return R::ok(inf.take());
}

}  // namespace nl
