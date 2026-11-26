#include "core/qr.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>

namespace nl {

namespace {

struct Spec {
    int total;   // Codewörter gesamt
    int ec;      // Fehlerkorrektur-Codewörter je Block
    int blocks;  // Anzahl Blöcke
};
// ISO/IEC 18004:2015 Tabelle 9, Fehlerkorrektur M, Versionen 1–10 (Index 0 unbenutzt).
constexpr std::array<Spec, 11> kSpec = {{{0, 0, 0},
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
constexpr std::array<std::array<int, 3>, 11> kAlign = {{{0, 0, 0},
                                                        {0, 0, 0},
                                                        {6, 18, 0},
                                                        {6, 22, 0},
                                                        {6, 26, 0},
                                                        {6, 30, 0},
                                                        {6, 34, 0},
                                                        {6, 22, 38},
                                                        {6, 24, 42},
                                                        {6, 26, 46},
                                                        {6, 28, 50}}};

int dataCodewords(int v) {
    const Spec& s = kSpec[static_cast<std::size_t>(v)];
    return s.total - s.ec * s.blocks;
}
int countBits(int v) { return v < 10 ? 8 : 16; }

u8 gfMul(u8 a, u8 b) {
    int r = 0;
    for (int i = 7; i >= 0; --i) {
        r = (r << 1) ^ ((r >> 7) * 0x11D);
        r ^= ((b >> i) & 1) * a;
    }
    return static_cast<u8>(r);
}

class Grid {
public:
    explicit Grid(int version) : v_(version), n_(17 + 4 * version), m_(cells(), 0), f_(cells(), 0) {}

    int size() const { return n_; }
    bool isFn(int x, int y) const { return f_[idx(x, y)] != 0; }
    bool get(int x, int y) const { return m_[idx(x, y)] != 0; }
    void set(int x, int y, bool d) { m_[idx(x, y)] = d ? 1 : 0; }
    void fn(int x, int y, bool d) {
        set(x, y, d);
        f_[idx(x, y)] = 1;
    }
    const std::vector<u8>& modules() const { return m_; }

    void drawFunctions() {
        for (int i = 0; i < n_; ++i) {
            fn(6, i, i % 2 == 0);
            fn(i, 6, i % 2 == 0);
        }
        finder(3, 3);
        finder(n_ - 4, 3);
        finder(3, n_ - 4);
        const auto& a = kAlign[static_cast<std::size_t>(v_)];
        const int count = v_ == 1 ? 0 : (v_ < 7 ? 2 : 3);
        for (int i = 0; i < count; ++i)
            for (int j = 0; j < count; ++j) {
                if ((i == 0 && j == 0) || (i == 0 && j == count - 1) || (i == count - 1 && j == 0)) continue;
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx)
                        fn(a[static_cast<std::size_t>(i)] + dx, a[static_cast<std::size_t>(j)] + dy,
                           std::max(std::abs(dx), std::abs(dy)) != 1);
            }
        format(0);  // reserviert den Bereich; die echte Maske kommt später
        if (v_ >= 7) {
            const u32 bits = qrVersionWord(v_);
            for (int i = 0; i < 18; ++i) {
                const bool d = ((bits >> i) & 1) != 0;
                fn(n_ - 11 + i % 3, i / 3, d);
                fn(i / 3, n_ - 11 + i % 3, d);
            }
        }
    }

    void format(int mask) {
        const u16 bits = qrFormatWord(mask);
        auto b = [&](int i) { return ((bits >> i) & 1) != 0; };
        for (int i = 0; i <= 5; ++i) fn(8, i, b(i));
        fn(8, 7, b(6));
        fn(8, 8, b(7));
        fn(7, 8, b(8));
        for (int i = 9; i < 15; ++i) fn(14 - i, 8, b(i));
        for (int i = 0; i < 8; ++i) fn(n_ - 1 - i, 8, b(i));
        for (int i = 8; i < 15; ++i) fn(8, n_ - 15 + i, b(i));
        fn(8, n_ - 8, true);  // dunkles Modul
    }

    void place(const std::vector<u8>& words) {
        std::size_t i = 0;
        const std::size_t bits = words.size() * 8;
        for (int right = n_ - 1; right >= 1; right -= 2) {
            if (right == 6) right = 5;
            for (int vert = 0; vert < n_; ++vert)
                for (int j = 0; j < 2; ++j) {
                    const int x = right - j;
                    const bool up = ((right + 1) & 2) == 0;
                    const int y = up ? n_ - 1 - vert : vert;
                    if (isFn(x, y) || i >= bits) continue;  // Restbits bleiben hell
                    set(x, y, ((words[i >> 3] >> (7 - (i & 7))) & 1) != 0);
                    ++i;
                }
        }
    }

    void applyMask(int m) {
        for (int y = 0; y < n_; ++y)
            for (int x = 0; x < n_; ++x) {
                if (isFn(x, y)) continue;
                bool flip = false;
                switch (m) {
                    case 0: flip = (x + y) % 2 == 0; break;
                    case 1: flip = y % 2 == 0; break;
                    case 2: flip = x % 3 == 0; break;
                    case 3: flip = (x + y) % 3 == 0; break;
                    case 4: flip = (x / 3 + y / 2) % 2 == 0; break;
                    case 5: flip = x * y % 2 + x * y % 3 == 0; break;
                    case 6: flip = (x * y % 2 + x * y % 3) % 2 == 0; break;
                    default: flip = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
                }
                if (flip) set(x, y, !get(x, y));
            }
    }

    // Strafpunkte N1–N4 (ISO/IEC 18004 7.8.3).
    long penalty() const {
        long p = 0;
        for (int pass = 0; pass < 2; ++pass)
            for (int a = 0; a < n_; ++a) {
                int run = 0;
                bool last = false;
                u32 window = 0;  // die letzten 11 Module der Zeile/Spalte
                for (int b = 0; b < n_; ++b) {
                    const bool d = pass == 0 ? get(b, a) : get(a, b);
                    if (b > 0 && d == last) {
                        ++run;
                    } else {
                        if (run >= 5) p += 3 + (run - 5);
                        run = 1;
                        last = d;
                    }
                    window = ((window << 1) | (d ? 1u : 0u)) & 0x7FF;
                    if (b >= 10 && (window == 0x5D0 || window == 0x05D)) p += 40;  // 1011101 mit 4 hellen
                }
                if (run >= 5) p += 3 + (run - 5);
            }
        int darkCount = 0;
        for (int y = 0; y < n_; ++y)
            for (int x = 0; x < n_; ++x) {
                if (get(x, y)) ++darkCount;
                if (x + 1 < n_ && y + 1 < n_) {
                    const bool c = get(x, y);
                    if (c == get(x + 1, y) && c == get(x, y + 1) && c == get(x + 1, y + 1)) p += 3;
                }
            }
        const int total = n_ * n_;
        const int k = (std::abs(darkCount * 20 - total * 10) + total - 1) / total - 1;
        p += static_cast<long>(std::max(0, k)) * 10;
        return p;
    }

private:
    std::size_t cells() const { return static_cast<std::size_t>((17 + 4 * v_) * (17 + 4 * v_)); }
    std::size_t idx(int x, int y) const { return static_cast<std::size_t>(y * n_ + x); }
    void finder(int cx, int cy) {
        for (int dy = -4; dy <= 4; ++dy)
            for (int dx = -4; dx <= 4; ++dx) {
                const int x = cx + dx, y = cy + dy;
                if (x < 0 || y < 0 || x >= n_ || y >= n_) continue;
                const int d = std::max(std::abs(dx), std::abs(dy));
                fn(x, y, d != 2 && d != 4);
            }
    }

    int v_, n_;
    std::vector<u8> m_, f_;
};

}  // namespace

std::vector<u8> qrRsEcc(std::span<const u8> data, int ecLen) {
    // Generatorpolynom (x − α^0)…(x − α^(ec−1)), führender Koeffizient 1 weggelassen.
    std::vector<u8> gen(static_cast<std::size_t>(ecLen), 0);
    gen.back() = 1;
    u8 root = 1;
    for (int i = 0; i < ecLen; ++i) {
        for (std::size_t j = 0; j < gen.size(); ++j) {
            gen[j] = gfMul(gen[j], root);
            if (j + 1 < gen.size()) gen[j] ^= gen[j + 1];
        }
        root = gfMul(root, 2);
    }
    std::vector<u8> rem(static_cast<std::size_t>(ecLen), 0);
    for (u8 b : data) {
        const u8 factor = static_cast<u8>(b ^ rem.front());
        rem.erase(rem.begin());
        rem.push_back(0);
        for (std::size_t j = 0; j < rem.size(); ++j) rem[j] ^= gfMul(gen[j], factor);
    }
    return rem;
}

u16 qrFormatWord(int mask) {
    const int data = (0 << 3) | (mask & 7);  // ECC M = 00
    int rem = data;
    for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    return static_cast<u16>(((data << 10) | rem) ^ 0x5412);
}

u32 qrVersionWord(int version) {
    int rem = version;
    for (int i = 0; i < 12; ++i) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
    return static_cast<u32>((version << 12) | rem);
}

std::optional<QrCode> encodeQr(std::string_view bytes) {
    int version = 0;
    for (int v = 1; v <= 10; ++v) {
        const std::size_t need = 4 + static_cast<std::size_t>(countBits(v)) + bytes.size() * 8;
        if (need <= static_cast<std::size_t>(dataCodewords(v)) * 8) {
            version = v;
            break;
        }
    }
    if (version == 0) return std::nullopt;

    // Bitstrom: Modus 0100, Länge, Bytes, Abschluss, Füllbytes.
    const std::size_t capBits = static_cast<std::size_t>(dataCodewords(version)) * 8;
    std::vector<u8> data(capBits / 8, 0);
    std::size_t pos = 0;
    auto put = [&](u32 val, int n) {
        for (int i = n - 1; i >= 0; --i, ++pos)
            if ((val >> i) & 1) data[pos >> 3] = static_cast<u8>(data[pos >> 3] | (0x80 >> (pos & 7)));
    };
    put(4, 4);
    put(static_cast<u32>(bytes.size()), countBits(version));
    for (char c : bytes) put(static_cast<u8>(c), 8);
    pos += std::min<std::size_t>(4, capBits - pos);  // Abschluss (Nullen)
    pos = (pos + 7) & ~static_cast<std::size_t>(7);
    for (bool alt = false; pos < capBits; pos += 8, alt = !alt) data[pos >> 3] = alt ? 0x11 : 0xEC;

    // Blöcke + Fehlerkorrektur, dann verflechten (kurze Blöcke zuerst).
    const Spec s = kSpec[static_cast<std::size_t>(version)];
    const int dataTotal = dataCodewords(version);
    const int shortLen = dataTotal / s.blocks;
    const int longCount = dataTotal % s.blocks;
    std::vector<std::vector<u8>> dataBlocks, ecBlocks;
    std::size_t off = 0;
    for (int b = 0; b < s.blocks; ++b) {
        const std::size_t len = static_cast<std::size_t>(shortLen + (b >= s.blocks - longCount ? 1 : 0));
        dataBlocks.emplace_back(data.begin() + static_cast<std::ptrdiff_t>(off),
                                data.begin() + static_cast<std::ptrdiff_t>(off + len));
        ecBlocks.push_back(qrRsEcc(dataBlocks.back(), s.ec));
        off += len;
    }
    std::vector<u8> words;
    words.reserve(static_cast<std::size_t>(s.total));
    for (int i = 0; i <= shortLen; ++i)
        for (const auto& blk : dataBlocks)
            if (static_cast<std::size_t>(i) < blk.size()) words.push_back(blk[static_cast<std::size_t>(i)]);
    for (int i = 0; i < s.ec; ++i)
        for (const auto& blk : ecBlocks) words.push_back(blk[static_cast<std::size_t>(i)]);

    Grid base(version);
    base.drawFunctions();
    base.place(words);
    int bestMask = 0;
    long bestPenalty = std::numeric_limits<long>::max();
    for (int m = 0; m < 8; ++m) {
        Grid g = base;
        g.applyMask(m);
        g.format(m);
        const long p = g.penalty();
        if (p < bestPenalty) {
            bestPenalty = p;
            bestMask = m;
        }
    }
    base.applyMask(bestMask);
    base.format(bestMask);
    QrCode q;
    q.size = base.size();
    q.modules = base.modules();
    return q;
}

}  // namespace nl
