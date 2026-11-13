#pragma once
// DSP-ADPCM (M18, Banner-Ton): 8-Byte-Frames aus einem Byte Prädiktor/Skala (Bits 4–6 Koeffizientenpaar,
// Bits 0–3 Exponent der Skala) und 14 vorzeichenbehafteten Nibbles, oberes Nibble zuerst.
// Sample = clamp((nibble · 2^exp · 2048 + c1 · hist1 + c2 · hist2 + 1024) >> 11).

#include <array>
#include <span>

#include "core/types.hpp"

namespace nl {

inline constexpr u32 kDspFrameBytes = 8;
inline constexpr u32 kDspFrameSamples = 14;

struct DspChannel {
    std::array<s16, 16> coef{};  // acht Paare (c1, c2)
    s16 hist1 = 0, hist2 = 0;
    u8 ps = 0;  // Prädiktor/Skala des letzten Frames
};

// Dekodiert out.size() Samples aus frames; fehlen Frames, wird der Rest mit 0 gefüllt. hist/ps laufen weiter.
void decodeDspAdpcm(std::span<const u8> frames, DspChannel& ch, std::span<s16> out);

}  // namespace nl
