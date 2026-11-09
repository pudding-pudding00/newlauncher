#include "core/easing.hpp"
#include <doctest/doctest.h>
using nl::easeOutCubic;
using nl::Tween;

TEST_CASE("easeOutCubic") {
    CHECK(easeOutCubic(0.0f) == 0.0f);
    CHECK(easeOutCubic(1.0f) == 1.0f);
    CHECK(easeOutCubic(0.5f) == doctest::Approx(0.875f));
    CHECK(easeOutCubic(-1.0f) == 0.0f);
    CHECK(easeOutCubic(2.0f) == 1.0f);
}

TEST_CASE("Tween erreicht Ziel nach Dauer") {
    Tween t;
    t.snap(0.0f);
    CHECK(t.done());
    t.to(10.0f, 200.0f);
    CHECK_FALSE(t.done());
    t.update(100.0f);
    CHECK(t.value() == doctest::Approx(8.75f));
    t.update(100.0f);
    CHECK(t.value() == 10.0f);
    CHECK(t.done());
}

TEST_CASE("Tween mit Dauer 0 springt") {
    Tween t;
    t.snap(3.0f);
    t.to(7.0f, 0.0f);
    CHECK(t.value() == 7.0f);
    CHECK(t.done());
}

TEST_CASE("Tween.to während einer Animation startet beim aktuellen Wert") {
    Tween t;
    t.snap(0.0f);
    t.to(10.0f, 200.0f);
    t.update(100.0f);
    t.to(0.0f, 200.0f);
    CHECK(t.value() == doctest::Approx(8.75f));
    t.update(200.0f);
    CHECK(t.value() == 0.0f);
}
