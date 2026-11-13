#include "core/dsp_adpcm.hpp"
#include "support/adpcm_encode.hpp"

#include <cmath>
#include <cstdlib>
#include <vector>

#include <doctest/doctest.h>

using namespace nl;

TEST_CASE("dsp_adpcm: handgerechneter Frame mit c1 = 2048 summiert die Nibbles auf") {
    DspChannel ch;
    ch.coef[2] = 2048;  // Paar 1: (2048, 0) → Vorhersage = hist1
    // ps: Paar 1, Exponent 1 (Skala 2); Nibbles 1, 2, -1 (0xF), -8 (0x8), 7, 0, …
    const std::vector<u8> frame{0x11, 0x12, 0xF8, 0x70, 0, 0, 0, 0};
    std::vector<s16> out(14);
    decodeDspAdpcm(frame, ch, out);
    CHECK(out[0] == 2);
    CHECK(out[1] == 6);
    CHECK(out[2] == 4);
    CHECK(out[3] == -12);
    CHECK(out[4] == 2);
    CHECK(out[5] == 2);
    CHECK(out[13] == 2);
    CHECK(ch.hist1 == 2);
    CHECK(ch.ps == 0x11);
}

TEST_CASE("dsp_adpcm: Encoder-Rekonstruktion bitgenau, Abweichung zum Original klein") {
    std::vector<s16> pcm(14 * 40 + 5);
    for (std::size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = static_cast<s16>(12000.0 * std::sin(static_cast<double>(i) * 0.07) + 3000.0 * std::sin(static_cast<double>(i) * 0.31));
    const auto e = test::encodeAdpcm(pcm);
    DspChannel ch;
    ch.coef = test::kAdpcmCoefs;
    std::vector<s16> out(pcm.size());
    decodeDspAdpcm(e.frames, ch, out);
    CHECK(out == e.decoded);
    int worst = 0;
    for (std::size_t i = 0; i < pcm.size(); ++i) worst = std::max(worst, std::abs(pcm[i] - out[i]));
    CHECK(worst < 600);  // weniger als 2 % der Aussteuerung
}

TEST_CASE("dsp_adpcm: Begrenzung auf s16 und fehlende Frames ergeben 0") {
    DspChannel ch;
    ch.coef[2] = 2048;
    ch.hist1 = 32000;
    const std::vector<u8> frame{0x1B, 0x77, 0, 0, 0, 0, 0, 0};  // Skala 2^11, Nibble 7
    std::vector<s16> out(20, 99);
    decodeDspAdpcm(frame, ch, out);
    CHECK(out[0] == 32767);
    CHECK(out[13] == 32767);
    CHECK(out[14] == 0);
    CHECK(out[19] == 0);
}
