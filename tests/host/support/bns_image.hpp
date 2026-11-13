#pragma once
// Baut BNS-Dateien für die Host-Tests (M18) mit dem eigenen DSP-ADPCM-Encoder.
// Feste Offsets im Ergebnis: INFO-Körper ab 0x28 (Kanäle 0x2A, Rate 0x2C, Schleifenstart 0x30, Samples 0x34).

#include <vector>

#include "core/types.hpp"
#include "support/adpcm_encode.hpp"

namespace nl::test {

inline constexpr std::size_t kBnsChannels = 0x2A, kBnsRate = 0x2C, kBnsLoopStart = 0x30, kBnsSamples = 0x34;

inline void bnsPut32(std::vector<u8>& b, std::size_t at, u32 v) {
    b[at] = static_cast<u8>(v >> 24);
    b[at + 1] = static_cast<u8>(v >> 16);
    b[at + 2] = static_cast<u8>(v >> 8);
    b[at + 3] = static_cast<u8>(v);
}
inline void bnsPut16(std::vector<u8>& b, std::size_t at, u16 v) {
    b[at] = static_cast<u8>(v >> 8);
    b[at + 1] = static_cast<u8>(v);
}

struct BnsBuilt {
    std::vector<u8> file;
    std::vector<std::vector<s16>> decoded;  // je Kanal, was der Decoder liefern muss
};

inline BnsBuilt buildBns(const std::vector<std::vector<s16>>& channels, u16 rate, bool loop, u32 loopStart) {
    BnsBuilt out;
    const u32 n = channels.empty() ? 0 : static_cast<u32>(channels[0].size());
    const u32 count = static_cast<u32>(channels.size());
    // INFO-Körper: 0x18 fest, Kanaltabelle, je Kanal 0x0C Kanalinfo und 0x30 ADPCM-Info.
    const u32 table = 0x18, chInfo = table + 4 * count, adpcm = chInfo + 0x0C * count;
    const u32 infoBody = adpcm + 0x30 * count;
    std::vector<u8> info(infoBody);
    info[0] = 0;  // Codec DSP-ADPCM
    info[1] = loop ? 1 : 0;
    info[2] = static_cast<u8>(count);
    bnsPut16(info, 4, rate);
    bnsPut32(info, 8, loopStart);
    bnsPut32(info, 12, n);
    bnsPut32(info, 16, table);
    std::vector<u8> data;
    for (u32 c = 0; c < count; ++c) {
        const auto e = encodeAdpcm(channels[c]);
        bnsPut32(info, table + 4 * c, chInfo + 0x0C * c);
        bnsPut32(info, chInfo + 0x0C * c, static_cast<u32>(data.size()));
        bnsPut32(info, chInfo + 0x0C * c + 4, adpcm + 0x30 * c);
        for (std::size_t k = 0; k < 16; ++k)
            bnsPut16(info, adpcm + 0x30 * c + 2 * k, static_cast<u16>(kAdpcmCoefs[k]));
        data.insert(data.end(), e.frames.begin(), e.frames.end());
        out.decoded.push_back(e.decoded);
    }
    const u32 infoSize = 8 + infoBody, dataSize = 8 + static_cast<u32>(data.size());
    std::vector<u8>& f = out.file;
    f.assign(0x20, 0);
    f[0] = 'B', f[1] = 'N', f[2] = 'S', f[3] = ' ';
    f[4] = 0xFE, f[5] = 0xFF, f[6] = 0x01, f[7] = 0x00;
    bnsPut32(f, 8, 0x20 + infoSize + dataSize);
    bnsPut16(f, 12, 0x20);
    bnsPut16(f, 14, 2);
    bnsPut32(f, 16, 0x20);
    bnsPut32(f, 20, infoSize);
    bnsPut32(f, 24, 0x20 + infoSize);
    bnsPut32(f, 28, dataSize);
    f.insert(f.end(), {'I', 'N', 'F', 'O', 0, 0, 0, 0});
    bnsPut32(f, 0x24, infoSize);
    f.insert(f.end(), info.begin(), info.end());
    const std::size_t d = f.size();
    f.insert(f.end(), {'D', 'A', 'T', 'A', 0, 0, 0, 0});
    bnsPut32(f, d + 4, dataSize);
    f.insert(f.end(), data.begin(), data.end());
    return out;
}

}  // namespace nl::test
