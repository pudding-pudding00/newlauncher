#include "core/theme.hpp"
#include <doctest/doctest.h>
#include <cstring>
using namespace nl;

TEST_CASE("Theme-Tokens laut Spec 3.6") {
    CHECK(lightTheme().bg == Color::fromHex(0xEEF1F4));
    CHECK(darkTheme().bg == Color::fromHex(0x121821));
    CHECK(lightTheme().accent == Color::fromHex(0x34BEED));
    CHECK(darkTheme().onAccent == Color::fromHex(0x0A1F2A));
    CHECK(lightTheme().overlayDim == Color::fromHex(0x000000, 0x73));
    CHECK(darkTheme().overlayDim == Color::fromHex(0x000000, 0x99));
}

TEST_CASE("isDarkAt") {
    CHECK(isDarkAt(ThemeMode::Auto, 23 * 60, 20 * 60, 7 * 60));
    CHECK(isDarkAt(ThemeMode::Auto, 6 * 60 + 59, 20 * 60, 7 * 60));
    CHECK_FALSE(isDarkAt(ThemeMode::Auto, 7 * 60, 20 * 60, 7 * 60));
    CHECK_FALSE(isDarkAt(ThemeMode::Auto, 12 * 60, 20 * 60, 7 * 60));
    CHECK(isDarkAt(ThemeMode::Auto, 3 * 60, 1 * 60, 5 * 60));
    CHECK_FALSE(isDarkAt(ThemeMode::Auto, 9 * 60, 9 * 60, 9 * 60));
    CHECK(isDarkAt(ThemeMode::Dark, 12 * 60, 20 * 60, 7 * 60));
    CHECK_FALSE(isDarkAt(ThemeMode::Light, 23 * 60, 20 * 60, 7 * 60));
}

TEST_CASE("lerp liefert die Endpunkte und liegt dazwischen") {
    const Theme& a = lightTheme();
    const Theme& b = darkTheme();
    CHECK(lerp(a, b, 0.0f).bg == a.bg);
    CHECK(lerp(a, b, 1.0f).bg == b.bg);
    CHECK(lerp(a, b, 1.0f).overlayDim == b.overlayDim);
    Color mid = lerp(a, b, 0.5f).bg;
    CHECK(mid.r < a.bg.r);
    CHECK(mid.r > b.bg.r);
}

TEST_CASE("ThemeState blendet in 200 ms um") {
    ThemeState s;
    s.setTarget(false, false);
    CHECK(s.current().bg == lightTheme().bg);
    s.setTarget(true, true);
    s.update(100.0f);
    CHECK(s.current().bg != lightTheme().bg);
    CHECK(s.current().bg != darkTheme().bg);
    s.update(100.0f);
    CHECK(s.current().bg == darkTheme().bg);
    s.setTarget(false, false);
    CHECK(s.current().bg == lightTheme().bg);
}

TEST_CASE("nextMode läuft im Kreis und hat Namen") {
    CHECK(nextMode(ThemeMode::Light) == ThemeMode::Dark);
    CHECK(nextMode(ThemeMode::Dark) == ThemeMode::Auto);
    CHECK(nextMode(ThemeMode::Auto) == ThemeMode::Light);
    CHECK(std::strcmp(themeModeName(ThemeMode::Auto), "Automatisch") == 0);
}
