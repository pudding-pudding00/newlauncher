#include "core/aes128.hpp"

#include <cstring>

namespace nl {

namespace {
// GF(2^8) mit dem Reduktionspolynom x^8 + x^4 + x^3 + x + 1 (FIPS-197 4.2).
constexpr u8 xtime(u8 x) { return static_cast<u8>((x << 1) ^ ((x & 0x80) != 0 ? 0x1B : 0)); }

constexpr u8 gmul(u8 a, u8 b) {
    u8 p = 0;
    while (b != 0) {
        if ((b & 1) != 0) p ^= a;
        a = xtime(a);
        b = static_cast<u8>(b >> 1);
    }
    return p;
}

constexpr u8 rotl8(u8 x, int n) { return static_cast<u8>((x << n) | (x >> (8 - n))); }

// S-Box (FIPS-197 5.1.1): multiplikatives Inverses (x^254), dann affine Abbildung mit 0x63.
constexpr std::array<u8, 256> makeSbox() {
    std::array<u8, 256> s{};
    for (int x = 0; x < 256; ++x) {
        u8 inv = 1, base = static_cast<u8>(x);
        for (int e = 254; e != 0; e >>= 1) {
            if ((e & 1) != 0) inv = gmul(inv, base);
            base = gmul(base, base);
        }
        if (x == 0) inv = 0;
        s[static_cast<std::size_t>(x)] =
            static_cast<u8>(inv ^ rotl8(inv, 1) ^ rotl8(inv, 2) ^ rotl8(inv, 3) ^ rotl8(inv, 4) ^ 0x63);
    }
    return s;
}

constexpr std::array<u8, 256> invert(const std::array<u8, 256>& s) {
    std::array<u8, 256> r{};
    for (int x = 0; x < 256; ++x) r[s[static_cast<std::size_t>(x)]] = static_cast<u8>(x);
    return r;
}

constexpr std::array<u8, 256> mulTable(u8 k) {
    std::array<u8, 256> t{};
    for (int x = 0; x < 256; ++x) t[static_cast<std::size_t>(x)] = gmul(static_cast<u8>(x), k);
    return t;
}

constexpr std::array<u8, 256> kSbox = makeSbox();
constexpr std::array<u8, 256> kInvSbox = invert(kSbox);
constexpr std::array<u8, 256> kMul9 = mulTable(9), kMul11 = mulTable(11), kMul13 = mulTable(13), kMul14 = mulTable(14);
static_assert(kSbox[0x00] == 0x63 && kSbox[0x53] == 0xED && kSbox[0xFF] == 0x16, "S-Box nach FIPS-197 Abb. 7");

void addRoundKey(u8* s, const u8* k) {
    for (int i = 0; i < 16; ++i) s[i] ^= k[i];
}

// Zustand spaltenweise: s[r + 4c]. Zeile r rotiert um r nach links.
void subShift(u8* s) {
    u8 t[16];
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) t[r + 4 * c] = kSbox[s[r + 4 * ((c + r) % 4)]];
    std::memcpy(s, t, 16);
}

void invShiftSub(u8* s) {
    u8 t[16];
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) t[r + 4 * ((c + r) % 4)] = kInvSbox[s[r + 4 * c]];
    std::memcpy(s, t, 16);
}

void mixColumns(u8* s) {
    for (int c = 0; c < 4; ++c) {
        u8* a = s + 4 * c;
        const u8 a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3];
        a[0] = static_cast<u8>(xtime(a0) ^ xtime(a1) ^ a1 ^ a2 ^ a3);
        a[1] = static_cast<u8>(a0 ^ xtime(a1) ^ xtime(a2) ^ a2 ^ a3);
        a[2] = static_cast<u8>(a0 ^ a1 ^ xtime(a2) ^ xtime(a3) ^ a3);
        a[3] = static_cast<u8>(xtime(a0) ^ a0 ^ a1 ^ a2 ^ xtime(a3));
    }
}

void invMixColumns(u8* s) {
    for (int c = 0; c < 4; ++c) {
        u8* a = s + 4 * c;
        const u8 a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3];
        a[0] = static_cast<u8>(kMul14[a0] ^ kMul11[a1] ^ kMul13[a2] ^ kMul9[a3]);
        a[1] = static_cast<u8>(kMul9[a0] ^ kMul14[a1] ^ kMul11[a2] ^ kMul13[a3]);
        a[2] = static_cast<u8>(kMul13[a0] ^ kMul9[a1] ^ kMul14[a2] ^ kMul11[a3]);
        a[3] = static_cast<u8>(kMul11[a0] ^ kMul13[a1] ^ kMul9[a2] ^ kMul14[a3]);
    }
}

bool sizesOk(std::span<const u8> in, std::span<u8> out) { return in.size() == out.size() && in.size() % 16 == 0; }
}  // namespace

Aes128::Aes128(const AesBlock& key) {
    // Schlüsselexpansion (FIPS-197 5.2): 44 Wörter.
    std::memcpy(roundKeys_.data(), key.data(), 16);
    u8 rcon = 1;
    for (int i = 4; i < 44; ++i) {
        u8 w[4];
        std::memcpy(w, roundKeys_.data() + 4 * (i - 1), 4);
        if (i % 4 == 0) {
            const u8 t = w[0];
            w[0] = static_cast<u8>(kSbox[w[1]] ^ rcon);
            w[1] = kSbox[w[2]];
            w[2] = kSbox[w[3]];
            w[3] = kSbox[t];
            rcon = xtime(rcon);
        }
        for (int j = 0; j < 4; ++j) roundKeys_[static_cast<std::size_t>(4 * i + j)] = static_cast<u8>(roundKeys_[static_cast<std::size_t>(4 * (i - 4) + j)] ^ w[j]);
    }
}

void Aes128::encryptBlock(const u8* in, u8* out) const {
    u8 s[16];
    std::memcpy(s, in, 16);
    addRoundKey(s, roundKeys_.data());
    for (int round = 1; round < 10; ++round) {
        subShift(s);
        mixColumns(s);
        addRoundKey(s, roundKeys_.data() + 16 * round);
    }
    subShift(s);
    addRoundKey(s, roundKeys_.data() + 160);
    std::memcpy(out, s, 16);
}

void Aes128::decryptBlock(const u8* in, u8* out) const {
    u8 s[16];
    std::memcpy(s, in, 16);
    addRoundKey(s, roundKeys_.data() + 160);
    for (int round = 9; round > 0; --round) {
        invShiftSub(s);
        addRoundKey(s, roundKeys_.data() + 16 * round);
        invMixColumns(s);
    }
    invShiftSub(s);
    addRoundKey(s, roundKeys_.data());
    std::memcpy(out, s, 16);
}

bool aesCbcDecrypt(const Aes128& aes, const AesBlock& iv, std::span<const u8> in, std::span<u8> out) {
    if (!sizesOk(in, out)) return false;
    AesBlock prev = iv;
    for (std::size_t at = 0; at < in.size(); at += 16) {
        AesBlock c, p;
        std::memcpy(c.data(), in.data() + at, 16);  // vor dem Schreiben sichern: in == out erlaubt
        aes.decryptBlock(c.data(), p.data());
        for (int i = 0; i < 16; ++i) out[at + static_cast<std::size_t>(i)] = static_cast<u8>(p[static_cast<std::size_t>(i)] ^ prev[static_cast<std::size_t>(i)]);
        prev = c;
    }
    return true;
}

bool aesCbcEncrypt(const Aes128& aes, const AesBlock& iv, std::span<const u8> in, std::span<u8> out) {
    if (!sizesOk(in, out)) return false;
    AesBlock prev = iv;
    for (std::size_t at = 0; at < in.size(); at += 16) {
        AesBlock x;
        for (int i = 0; i < 16; ++i) x[static_cast<std::size_t>(i)] = static_cast<u8>(in[at + static_cast<std::size_t>(i)] ^ prev[static_cast<std::size_t>(i)]);
        aes.encryptBlock(x.data(), prev.data());
        std::memcpy(out.data() + at, prev.data(), 16);
    }
    return true;
}

}  // namespace nl
