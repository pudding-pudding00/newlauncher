#include "core/bns.hpp"
#include "support/bns_image.hpp"

#include <cmath>
#include <vector>

#include <doctest/doctest.h>

using namespace nl;

namespace {
std::vector<s16> tone(std::size_t n, double step, double amp) {
    std::vector<s16> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<s16>(amp * std::sin(static_cast<double>(i) * step));
    return v;
}
}  // namespace

TEST_CASE("bns: Stereo wird verschränkt dekodiert") {
    const auto built = test::buildBns({tone(100, 0.1, 8000), tone(100, 0.05, -5000)}, 32000, true, 28);
    const auto r = parseBns(built.file, kBnsMaxSamples);
    REQUIRE(r.isOk());
    const BnsSound& s = r.value();
    CHECK(s.sampleRate == 32000);
    CHECK(s.channels == 2);
    CHECK(s.loop);
    CHECK(s.loopStart == 28);
    REQUIRE(s.pcm.size() == 200);
    for (std::size_t i = 0; i < 100; ++i) {
        CHECK(s.pcm[2 * i] == built.decoded[0][i]);
        CHECK(s.pcm[2 * i + 1] == built.decoded[1][i]);
    }
}

TEST_CASE("bns: Mono liegt auf beiden Seiten") {
    const auto built = test::buildBns({tone(50, 0.2, 10000)}, 22050, false, 0);
    const auto r = parseBns(built.file, kBnsMaxSamples);
    REQUIRE(r.isOk());
    const BnsSound& s = r.value();
    CHECK(s.channels == 2);
    CHECK_FALSE(s.loop);
    REQUIRE(s.pcm.size() == 100);
    for (std::size_t i = 0; i < 50; ++i) {
        CHECK(s.pcm[2 * i] == built.decoded[0][i]);
        CHECK(s.pcm[2 * i + 1] == built.decoded[0][i]);
    }
}

TEST_CASE("bns: Schleifenstart hinter dem Ende schaltet die Schleife ab") {
    auto built = test::buildBns({tone(40, 0.2, 1000)}, 32000, true, 40);
    const auto r = parseBns(built.file, kBnsMaxSamples);
    REQUIRE(r.isOk());
    CHECK_FALSE(r.value().loop);
    CHECK(r.value().loopStart == 0);
}

TEST_CASE("bns: 3 Kanäle, 0 Kanäle, Rate 0, Codec ≠ 0 und falsches Magic sind Fehler") {
    const auto base = test::buildBns({tone(30, 0.2, 1000)}, 32000, false, 0).file;
    auto f = base;
    f[test::kBnsChannels] = 3;
    CHECK_FALSE(parseBns(f, kBnsMaxSamples).isOk());
    f = base;
    f[test::kBnsChannels] = 0;
    CHECK_FALSE(parseBns(f, kBnsMaxSamples).isOk());
    f = base;
    f[test::kBnsRate] = 0, f[test::kBnsRate + 1] = 0;
    CHECK_FALSE(parseBns(f, kBnsMaxSamples).isOk());
    f = base;
    f[test::kBnsChannels - 2] = 2;  // Codec PCM16 wird nicht unterstützt
    CHECK_FALSE(parseBns(f, kBnsMaxSamples).isOk());
    f = base;
    f[0] = 'X';
    CHECK_FALSE(parseBns(f, kBnsMaxSamples).isOk());
}

TEST_CASE("bns: maxSamples, abgeschnittene Daten und kaputte Offsets sind Fehler") {
    const auto base = test::buildBns({tone(100, 0.2, 1000), tone(100, 0.3, 1000)}, 32000, false, 0).file;
    CHECK(parseBns(base, 100).isOk());
    CHECK_FALSE(parseBns(base, 99).isOk());
    auto f = base;
    f.resize(f.size() - 1);
    CHECK_FALSE(parseBns(f, kBnsMaxSamples).isOk());
    f = base;
    test::bnsPut32(f, test::kBnsSamples, 0xFFFFFFFFu);
    CHECK_FALSE(parseBns(f, 0xFFFFFFFFu).isOk());
    f = base;
    test::bnsPut32(f, 0x28 + 0x18, 0x7FFFFFF0u);  // Kanaltabelle zeigt ins Leere
    CHECK_FALSE(parseBns(f, kBnsMaxSamples).isOk());
    for (std::size_t cut = 0; cut < base.size(); cut += 7) {
        const std::vector<u8> part(base.begin(), base.begin() + static_cast<std::ptrdiff_t>(cut));
        CHECK_FALSE(parseBns(part, kBnsMaxSamples).isOk());
    }
}
