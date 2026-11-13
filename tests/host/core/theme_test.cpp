#include "core/theme.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
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

namespace {
double channel(u8 v) {
    const double c = v / 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
double luminance(Color c) { return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b); }
double contrast(Color a, Color b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}
}  // namespace

TEST_CASE("danger: Spec-Werte und Kontrast >= 3 zur Leiste") {
    CHECK(lightTheme().danger == Color::fromHex(0xD93025));
    CHECK(darkTheme().danger == Color::fromHex(0xF28B82));
    CHECK(contrast(lightTheme().danger, lightTheme().bar) >= 3.0);
    CHECK(contrast(darkTheme().danger, darkTheme().bar) >= 3.0);
    CHECK(lerp(lightTheme(), darkTheme(), 1.0f).danger == darkTheme().danger);
}

TEST_CASE("ThemeState: eigenes Paar ersetzt hell und dunkel, auch mitten im Übergang") {
    Theme l = lightTheme(), d = darkTheme();
    l.bar = Color{1, 2, 3, 255};
    d.bar = Color{9, 8, 7, 255};
    ThemeState s;
    s.setTarget(false, false);
    s.setThemes(l, d);
    CHECK(s.current().bar == Color{1, 2, 3, 255});
    s.setTarget(true, false);
    CHECK(s.current().bar == Color{9, 8, 7, 255});
    s.setThemes(lightTheme(), darkTheme());
    CHECK(s.current() == darkTheme());
}
