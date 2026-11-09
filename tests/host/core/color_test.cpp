#include "core/color.hpp"
#include <doctest/doctest.h>
using nl::Color;

TEST_CASE("Color::fromHex zerlegt RGB, Alpha standardmäßig deckend") {
    constexpr Color c = Color::fromHex(0x34BEED);
    static_assert(c.r == 0x34 && c.g == 0xBE && c.b == 0xED && c.a == 0xFF);
    CHECK(c == Color{0x34, 0xBE, 0xED, 0xFF});
}
TEST_CASE("Color::fromHex übernimmt Alpha") {
    CHECK(Color::fromHex(0x000000, 0x73) == Color{0, 0, 0, 0x73});
}
