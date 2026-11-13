#pragma once
// Eigener DSP-ADPCM-Encoder für die Host-Tests (M18). Probiert je Frame alle Koeffizientenpaare und Skalen
// und nimmt die kleinste Fehlerquadratsumme. Führt die Rekonstruktion mit, die ein korrekter Decoder
// bitgenau treffen muss. Gleiches Verfahren wie tools/make_banner_fixtures.py.

#include <algorithm>
#include <array>
#include <vector>

#include "core/types.hpp"

namespace nl::test {

inline constexpr std::array<s16, 16> kAdpcmCoefs{0,    0,     2048, 0,     4096, -2048, 3600, -1600,
                                                 3900, -1900, 1800, 0,     3000, -1000, 4000, -2000};

struct Encoded {
    std::vector<u8> frames;
    std::vector<s16> decoded;  // was der Decoder liefern muss
};

inline s16 adpcmSample(int nib, int exp, int c1, int c2, int h1, int h2) {
    const int v = ((nib * (1 << exp)) * 2048 + c1 * h1 + c2 * h2 + 1024) >> 11;
    return static_cast<s16>(std::clamp(v, -32768, 32767));
}

inline Encoded encodeAdpcm(const std::vector<s16>& pcm) {
    Encoded e;
    int h1 = 0, h2 = 0;
    for (std::size_t at = 0; at < pcm.size(); at += 14) {
        long long bestErr = -1;
        u8 bestPs = 0;
        std::array<int, 14> bestNib{};
        std::array<s16, 14> bestOut{};
        for (int p = 0; p < 8; ++p)
            for (int exp = 0; exp < 12; ++exp) {
                const int c1 = kAdpcmCoefs[static_cast<std::size_t>(p * 2)], c2 = kAdpcmCoefs[static_cast<std::size_t>(p * 2 + 1)];
                int a = h1, b = h2;
                long long err = 0;
                std::array<int, 14> nib{};
                std::array<s16, 14> out{};
                for (std::size_t k = 0; k < 14; ++k) {
                    const int x = at + k < pcm.size() ? pcm[at + k] : 0;
                    const int pred = (c1 * a + c2 * b + 1024) >> 11;
                    const int step = 1 << exp;
                    int n = (x - pred) >= 0 ? ((x - pred) + step / 2) / step : -((pred - x + step / 2) / step);
                    n = std::clamp(n, -8, 7);
                    const s16 s = adpcmSample(n, exp, c1, c2, a, b);
                    err += static_cast<long long>(x - s) * (x - s);
                    nib[k] = n;
                    out[k] = s;
                    b = a;
                    a = s;
                }
                if (bestErr < 0 || err < bestErr) {
                    bestErr = err;
                    bestPs = static_cast<u8>(p << 4 | exp);
                    bestNib = nib;
                    bestOut = out;
                }
            }
        e.frames.push_back(bestPs);
        for (std::size_t k = 0; k < 14; k += 2)
            e.frames.push_back(static_cast<u8>((bestNib[k] & 0xF) << 4 | (bestNib[k + 1] & 0xF)));
        for (std::size_t k = 0; k < 14 && at + k < pcm.size(); ++k) e.decoded.push_back(bestOut[k]);
        h1 = bestOut[13];
        h2 = bestOut[12];
    }
    return e;
}

}  // namespace nl::test
