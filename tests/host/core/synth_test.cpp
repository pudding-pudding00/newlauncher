#include "core/synth.hpp"
#include "core/ui_sound.hpp"
#include <doctest/doctest.h>
#include <cstdlib>
#include <string>
using namespace nl;

namespace {
int zeroCrossings(const std::vector<s16>& s, std::size_t from, std::size_t to) {
    int n = 0;
    for (std::size_t i = from + 1; i < to; ++i)
        if ((s[i - 1] < 0) != (s[i] < 0)) ++n;
    return n;
}
}  // namespace

TEST_CASE("uiSoundSpec: feste Werte und Namen") {
    const ToneSpec m = uiSoundSpec(UiSound::Move);
    CHECK(m.startHz == 1200.0f);
    CHECK(m.endHz == 1200.0f);
    CHECK(m.ms == 25);
    CHECK(m.amplitude == doctest::Approx(0.20f));
    const ToneSpec e = uiSoundSpec(UiSound::Error);
    CHECK(e.startHz == 220.0f);
    CHECK(e.endHz == 196.0f);
    CHECK(e.ms == 160);
    CHECK(e.amplitude == doctest::Approx(0.35f));
    CHECK(std::string(uiSoundName(UiSound::Move)) == "move");
    CHECK(std::string(uiSoundName(UiSound::Confirm)) == "confirm");
    CHECK(std::string(uiSoundName(UiSound::Back)) == "back");
    CHECK(std::string(uiSoundName(UiSound::Open)) == "open");
    CHECK(std::string(uiSoundName(UiSound::Error)) == "error");
}

TEST_CASE("synthTone: Länge auf 16 Samples aufgerundet") {
    CHECK(synthTone(uiSoundSpec(UiSound::Move), 48000).size() == 1200);
    CHECK(synthTone(uiSoundSpec(UiSound::Open), 48000).size() == 5280);
    CHECK(synthTone(ToneSpec{440, 440, 1, 0.5f}, 48000).size() == 48);  // 48 ist schon ein Vielfaches
    CHECK(synthTone(ToneSpec{440, 440, 1, 0.5f}, 44100).size() == 48);  // 44,1 → 48
}

TEST_CASE("synthTone: beginnt und endet bei 0, Amplitude begrenzt") {
    for (UiSound u : {UiSound::Move, UiSound::Confirm, UiSound::Back, UiSound::Open, UiSound::Error}) {
        const std::vector<s16> s = synthTone(uiSoundSpec(u), 48000);
        REQUIRE(!s.empty());
        CHECK(s.front() == 0);
        CHECK(s.back() == 0);
        int peak = 0;
        for (s16 v : s) peak = std::max(peak, std::abs(static_cast<int>(v)));
        CHECK(peak <= static_cast<int>(0.35f * 32767) + 1);
        CHECK(peak > 1000);  // hörbar
    }
}

TEST_CASE("synthTone: Confirm steigt in der Frequenz") {
    const std::vector<s16> s = synthTone(uiSoundSpec(UiSound::Confirm), 48000);
    const std::size_t half = s.size() / 2;
    const double first = static_cast<double>(zeroCrossings(s, 0, half)) / static_cast<double>(half);
    const double second = static_cast<double>(zeroCrossings(s, half, s.size())) / static_cast<double>(s.size() - half);
    CHECK(second > first);
}
