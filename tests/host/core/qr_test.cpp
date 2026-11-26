#include "core/qr.hpp"

#include <doctest/doctest.h>

#include <array>
#include <string>
#include <vector>

using namespace nl;

namespace {

// ---- kleiner, unabhängiger Prüf-Decoder (nur Byte-Modus, ECC M, Version 1–10) ----

struct BlockSpec {
    int total;  // Codewörter gesamt
    int ec;     // Fehlerkorrektur je Block
    int blocks;
};
// ISO/IEC 18004 Tabelle 9, Spalte M.
constexpr std::array<BlockSpec, 11> kSpecM = {{{0, 0, 0},
                                               {26, 10, 1},
                                               {44, 16, 1},
                                               {70, 26, 1},
                                               {100, 18, 2},
                                               {134, 24, 2},
                                               {172, 16, 4},
                                               {196, 18, 4},
                                               {242, 22, 4},
                                               {292, 22, 5},
                                               {346, 26, 5}}};

u8 gfMul(u8 a, u8 b) {
    int r = 0;
    for (int i = 7; i >= 0; --i) {
        r = (r << 1) ^ ((r >> 7) * 0x11D);
        r ^= ((b >> i) & 1) * a;
    }
    return static_cast<u8>(r);
}

std::vector<int> alignPos(int version) {
    static const std::array<std::vector<int>, 11> t = {{{},
                                                        {},
                                                        {6, 18},
                                                        {6, 22},
                                                        {6, 26},
                                                        {6, 30},
                                                        {6, 34},
                                                        {6, 22, 38},
                                                        {6, 24, 42},
                                                        {6, 26, 46},
                                                        {6, 28, 50}}};
    return t[static_cast<std::size_t>(version)];
}

std::vector<bool> functionMask(int version, int size) {
    std::vector<bool> f(static_cast<std::size_t>(size * size), false);
    auto mark = [&](int x, int y) {
        if (x >= 0 && y >= 0 && x < size && y < size) f[static_cast<std::size_t>(y * size + x)] = true;
    };
    for (int i = 0; i < size; ++i) {
        mark(6, i);
        mark(i, 6);
    }
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 9; ++x) mark(x, y);  // links oben inkl. Trennlinie und Formatbereich
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 8; ++x) {
            mark(size - 8 + x, y);  // rechts oben + Format in Zeile 8
            mark(y, size - 8 + x);  // links unten + Format in Spalte 8 (mit dunklem Modul)
        }
    const auto a = alignPos(version);
    for (std::size_t i = 0; i < a.size(); ++i)
        for (std::size_t j = 0; j < a.size(); ++j) {
            if ((i == 0 && j == 0) || (i == 0 && j + 1 == a.size()) || (i + 1 == a.size() && j == 0)) continue;
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx) mark(a[i] + dx, a[j] + dy);
        }
    if (version >= 7)
        for (int i = 0; i < 6; ++i)
            for (int k = 0; k < 3; ++k) {
                mark(size - 11 + k, i);
                mark(i, size - 11 + k);
            }
    return f;
}

bool maskBit(int m, int x, int y) {
    switch (m) {
        case 0: return (x + y) % 2 == 0;
        case 1: return y % 2 == 0;
        case 2: return x % 3 == 0;
        case 3: return (x + y) % 3 == 0;
        case 4: return (x / 3 + y / 2) % 2 == 0;
        case 5: return x * y % 2 + x * y % 3 == 0;
        case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
        default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
    }
}

// Liefert die Nutzdaten oder einen Fehlertext mit '!' am Anfang.
std::string decode(const QrCode& q) {
    const int size = q.size;
    if (size < 21 || (size - 17) % 4 != 0) return "!size";
    const int version = (size - 17) / 4;
    if (version > 10) return "!version";

    // Formatinfo (erste Kopie) lesen und per BCH prüfen.
    int fmt = 0;
    auto put = [&](int i, int x, int y) { fmt |= (q.dark(x, y) ? 1 : 0) << i; };
    for (int i = 0; i <= 5; ++i) put(i, 8, i);
    put(6, 8, 7);
    put(7, 8, 8);
    put(8, 7, 8);
    for (int i = 9; i < 15; ++i) put(i, 14 - i, 8);
    fmt ^= 0x5412;
    int rem = fmt >> 10;
    const int data5 = rem;
    for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    if (((data5 << 10) | rem) != fmt) return "!format-bch";
    if ((data5 >> 3) != 0) return "!not-M";
    const int mask = data5 & 7;

    // Zickzack lesen.
    const auto f = functionMask(version, size);
    std::vector<u8> words;
    int bit = 0;
    u8 cur = 0;
    for (int right = size - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (int v = 0; v < size; ++v)
            for (int j = 0; j < 2; ++j) {
                const int x = right - j;
                const bool up = ((right + 1) & 2) == 0;
                const int y = up ? size - 1 - v : v;
                if (f[static_cast<std::size_t>(y * size + x)]) continue;
                const bool d = q.dark(x, y) != maskBit(mask, x, y);
                cur = static_cast<u8>((cur << 1) | (d ? 1 : 0));
                if (++bit == 8) {
                    words.push_back(cur);
                    bit = 0;
                    cur = 0;
                }
            }
    }
    const BlockSpec s = kSpecM[static_cast<std::size_t>(version)];
    if (static_cast<int>(words.size()) < s.total) return "!short";
    words.resize(static_cast<std::size_t>(s.total));

    // Blöcke entflechten.
    const int dataTotal = s.total - s.ec * s.blocks;
    const int shortLen = dataTotal / s.blocks;
    const int longCount = dataTotal % s.blocks;
    std::vector<std::vector<u8>> blocks(static_cast<std::size_t>(s.blocks));
    std::size_t k = 0;
    for (int i = 0; i <= shortLen; ++i)
        for (int b = 0; b < s.blocks; ++b) {
            const int len = shortLen + (b >= s.blocks - longCount ? 1 : 0);
            if (i < len) blocks[static_cast<std::size_t>(b)].push_back(words[k++]);
        }
    for (int i = 0; i < s.ec; ++i)
        for (int b = 0; b < s.blocks; ++b) blocks[static_cast<std::size_t>(b)].push_back(words[k++]);

    // RS-Syndrome: Codewort-Polynom an α^0 … α^(ec-1) muss 0 sein.
    std::vector<u8> data;
    for (const auto& blk : blocks) {
        u8 alpha = 1;
        for (int i = 0; i < s.ec; ++i) {
            u8 acc = 0;
            for (u8 c : blk) acc = static_cast<u8>(gfMul(acc, alpha) ^ c);
            if (acc != 0) return "!syndrome";
            alpha = gfMul(alpha, 2);
        }
        data.insert(data.end(), blk.begin(), blk.end() - s.ec);
    }

    // Byte-Modus lesen.
    std::size_t pos = 0;
    auto bits = [&](int n) {
        int v = 0;
        for (int i = 0; i < n; ++i, ++pos) v = (v << 1) | ((data[pos >> 3] >> (7 - (pos & 7))) & 1);
        return v;
    };
    if (bits(4) != 4) return "!mode";
    const int count = bits(version < 10 ? 8 : 16);
    if (pos + static_cast<std::size_t>(count) * 8 > data.size() * 8) return "!count";
    std::string out;
    for (int i = 0; i < count; ++i) out.push_back(static_cast<char>(bits(8)));
    return out;
}

std::string sample(std::size_t n) {
    std::string s;
    for (std::size_t i = 0; i < n; ++i) s.push_back(static_cast<char>((i * 37 + 11) & 0xFF));
    return s;
}

}  // namespace

TEST_CASE("qrRsEcc: HELLO WORLD 1-M") {
    const std::vector<u8> data = {32, 91, 11, 120, 209, 114, 220, 77, 67, 64, 236, 17, 236, 17, 236, 17};
    CHECK(qrRsEcc(data, 10) == std::vector<u8>{196, 35, 39, 119, 235, 215, 231, 226, 93, 23});
}

TEST_CASE("qrFormatWord und qrVersionWord") {
    CHECK(qrFormatWord(0) == 0b101010000010010);
    CHECK(qrVersionWord(7) == 0b000111110010010100);
}

TEST_CASE("encodeQr: Version nach Länge, Größe 21 + 4(v-1)") {
    const std::array<std::pair<std::size_t, int>, 5> cases = {{{0, 21}, {1, 21}, {17, 25}, {100, 41}, {213, 57}}};
    for (const auto& [n, size] : cases) {
        CAPTURE(n);
        const auto q = encodeQr(sample(n));
        REQUIRE(q.has_value());
        CHECK(q->size == size);
        CHECK(q->modules.size() == static_cast<std::size_t>(size * size));
    }
    CHECK_FALSE(encodeQr(sample(214)).has_value());
}

TEST_CASE("encodeQr: Suchmuster, Timing, dunkles Modul") {
    const auto q = encodeQr("https://example.com/x");
    REQUIRE(q.has_value());
    const int n = q->size;
    for (const auto& [ox, oy] : std::array<std::pair<int, int>, 3>{{{0, 0}, {n - 7, 0}, {0, n - 7}}}) {
        CHECK(q->dark(ox, oy));
        CHECK(q->dark(ox + 6, oy + 6));
        CHECK_FALSE(q->dark(ox + 1, oy + 1));
        CHECK(q->dark(ox + 3, oy + 3));
    }
    CHECK_FALSE(q->dark(7, 0));  // Trennlinie
    CHECK_FALSE(q->dark(0, 7));
    for (int i = 8; i < n - 8; ++i) {
        CHECK(q->dark(i, 6) == (i % 2 == 0));
        CHECK(q->dark(6, i) == (i % 2 == 0));
    }
    CHECK(q->dark(8, n - 8));
}

TEST_CASE("encodeQr: Rundlauf über den Prüf-Decoder") {
    for (std::size_t n : {1u, 17u, 100u, 213u}) {
        CAPTURE(n);
        const std::string in = sample(n);
        const auto q = encodeQr(in);
        REQUIRE(q.has_value());
        CHECK(decode(*q) == in);
    }
    const std::string url = "http://192.0.2.10:4301/AbCdEf0123456789-_xyz";
    const auto q = encodeQr(url);
    REQUIRE(q.has_value());
    CHECK(decode(*q) == url);
}

TEST_CASE("encodeQr: Versionsinfo ab Version 7 lesbar") {
    const auto q = encodeQr(sample(150));  // V8
    REQUIRE(q.has_value());
    const int n = q->size;
    u32 v = 0;
    for (int i = 0; i < 18; ++i) v |= static_cast<u32>(q->dark(n - 11 + i % 3, i / 3) ? 1 : 0) << i;
    CHECK(v == qrVersionWord((n - 17) / 4));
    u32 w = 0;
    for (int i = 0; i < 18; ++i) w |= static_cast<u32>(q->dark(i / 3, n - 11 + i % 3) ? 1 : 0) << i;
    CHECK(w == v);
}
