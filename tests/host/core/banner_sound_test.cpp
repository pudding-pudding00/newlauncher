#include "core/banner_sound.hpp"
#include "core/playlist.hpp"

#include <doctest/doctest.h>

using namespace nl;
using A = BannerSoundGate::Action;

TEST_CASE("banner_sound: Start erst nach 1 s Fokus, dann einmal") {
    BannerSoundGate g;
    CHECK(g.update(true, true, 0) == A::None);
    CHECK(g.update(true, true, 999) == A::None);
    CHECK_FALSE(g.playing());
    CHECK(g.update(true, true, 1000) == A::Start);
    CHECK(g.playing());
    CHECK(g.update(true, true, 5000) == A::None);
}

TEST_CASE("banner_sound: Klänge aus oder Ton weg stoppt; ohne Ton nie Start") {
    BannerSoundGate g;
    CHECK(g.update(false, true, 2000) == A::None);
    CHECK(g.update(true, false, 2000) == A::None);
    CHECK(g.update(true, true, 2000) == A::Start);
    CHECK(g.update(true, false, 2100) == A::Stop);
    CHECK_FALSE(g.playing());
    CHECK(g.update(true, true, 2200) == A::Start);
    CHECK(g.update(false, true, 2300) == A::Stop);
    g.update(true, true, 2400);
    g.reset();
    CHECK_FALSE(g.playing());
    CHECK(g.update(true, true, 0) == A::None);
}

TEST_CASE("duckVolume: 20 % der eingestellten Lautstärke") {
    CHECK(duckVolume(255) == 51);
    CHECK(duckVolume(153) == 30);
    CHECK(duckVolume(0) == 0);
}

TEST_CASE("banner_sound: Ende eines Tons ohne Schleife hebt die Dämpfung auf, kein Neustart") {
    BannerSoundGate g;
    CHECK(g.update(true, true, 1000, false) == A::Start);
    CHECK(g.update(true, true, 1500, false) == A::None);
    CHECK(g.update(true, true, 4000, true) == A::Stop);  // Stimme frei: Musik wieder laut
    CHECK_FALSE(g.playing());
    CHECK(g.update(true, true, 4100, true) == A::None);
    CHECK(g.update(true, true, 4200, false) == A::None);  // bleibt aus bis zum nächsten Spiel
    g.reset();
    CHECK(g.update(true, true, 1000, false) == A::Start);
}
