#include "core/dsp_adpcm.hpp"

#include <algorithm>

namespace nl {

void decodeDspAdpcm(std::span<const u8> frames, DspChannel& ch, std::span<s16> out) {
    std::size_t done = 0;
    for (std::size_t at = 0; at + kDspFrameBytes <= frames.size() && done < out.size(); at += kDspFrameBytes) {
        ch.ps = frames[at];
        const std::size_t pair = static_cast<std::size_t>((ch.ps >> 4) & 7) * 2;
        const int c1 = ch.coef[pair], c2 = ch.coef[pair + 1];
        const int scale = 1 << (ch.ps & 0xF);
        for (std::size_t k = 0; k < kDspFrameSamples && done < out.size(); ++k) {
            const u8 b = frames[at + 1 + k / 2];
            int nib = (k % 2 == 0) ? (b >> 4) : (b & 0xF);
            if (nib >= 8) nib -= 16;
            const int v = (nib * scale * 2048 + c1 * ch.hist1 + c2 * ch.hist2 + 1024) >> 11;
            const s16 s = static_cast<s16>(std::clamp(v, -32768, 32767));
            ch.hist2 = ch.hist1;
            ch.hist1 = s;
            out[done++] = s;
        }
    }
    std::fill(out.begin() + static_cast<std::ptrdiff_t>(done), out.end(), s16{0});
}

}  // namespace nl
