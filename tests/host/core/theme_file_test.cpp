#include "core/theme_file.hpp"

#include <doctest/doctest.h>

#include <cstdio>

#include "core/config.hpp"
#include "core/ini.hpp"
#include "support/mem_fs.hpp"

using namespace nl;

namespace {
std::vector<u8> bytes(std::string_view s) { return {s.begin(), s.end()}; }
}  // namespace

TEST_CASE("parseHexColor: #RRGGBB und #RRGGBBAA, Groß/klein egal, getrimmt") {
    CHECK(parseHexColor("#FfFfFf") == Color{0xFF, 0xFF, 0xFF, 0xFF});
    CHECK(parseHexColor("  #102030  ") == Color{0x10, 0x20, 0x30, 0xFF});
    CHECK(parseHexColor("#10203040") == Color{0x10, 0x20, 0x30, 0x40});
    CHECK_FALSE(parseHexColor("ffffff"));
    CHECK_FALSE(parseHexColor("#12345"));
    CHECK_FALSE(parseHexColor("#1234567"));
    CHECK_FALSE(parseHexColor("#fff"));
    CHECK_FALSE(parseHexColor("#gg0000"));
    CHECK_FALSE(parseHexColor("# 102030"));
    CHECK_FALSE(parseHexColor(""));
}

TEST_CASE("themeFromIni: alle 14 Tokens gesetzt → genau diese Farben") {
    std::string text = "[light]\n";
    for (std::size_t i = 0; i < kThemeTokenCount; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "#%02X0000", static_cast<unsigned>(i + 1));
        text += std::string(kThemeTokenNames[i]) + " = " + buf + "\n";
    }
    std::vector<std::string> bad;
    const ThemePair p = themeFromIni(IniDoc::parse(text), &bad);
    CHECK(bad.empty());
    for (std::size_t i = 0; i < kThemeTokenCount; ++i)
        CHECK(themeToken(p.light, i) == Color{static_cast<u8>(i + 1), 0, 0, 0xFF});
    CHECK(p.light.danger == Color{14, 0, 0, 0xFF});
    CHECK(p.dark == darkTheme());
}

TEST_CASE("themeFromIni: kaputter Wert nimmt das Standard-Token und meldet ihn") {
    std::vector<std::string> bad;
    const ThemePair p = themeFromIni(IniDoc::parse("[light]\nbar_border = ffffff\nbar = #FfFfFf\n"), &bad);
    CHECK(bad == std::vector<std::string>{"light.bar_border"});
    CHECK(p.light.barBorder == lightTheme().barBorder);
    CHECK(p.light.bar == Color{0xFF, 0xFF, 0xFF, 0xFF});
}

TEST_CASE("themeFromIni: nur [dark] → light ist komplett Standard") {
    std::vector<std::string> bad;
    const ThemePair p = themeFromIni(IniDoc::parse("[dark]\naccent = #11223344\ntile = #12345\n"), &bad);
    CHECK(p.light == lightTheme());
    CHECK(p.dark.accent == Color{0x11, 0x22, 0x33, 0x44});
    CHECK(p.dark.tile == darkTheme().tile);
    CHECK(bad == std::vector<std::string>{"dark.tile"});
}

TEST_CASE("themeFromIni: unbekannte Schlüssel und Abschnitte werden gemeldet, fehlende sind kein Fehler") {
    std::vector<std::string> bad;
    const ThemePair p =
        themeFromIni(IniDoc::parse("; Kopf\n[light]\nfoo = #000000\n[Dark]\naccent = #000000\n[dark]\n"), &bad);
    CHECK(bad == std::vector<std::string>{"light.foo", "Dark"});
    CHECK(p.light == lightTheme());
    CHECK(p.dark == darkTheme());
    bad.clear();
    themeFromIni(IniDoc::parse("[light]\naccent = #112233\n"), &bad);
    CHECK(bad.empty());
}

TEST_CASE("listThemePacks: nur Ordner mit theme.ini, sortiert, höchstens 32") {
    test::MemFileSystem fs;
    fs.addFile("sd:/nl/themes/zeta/theme.ini", bytes("[light]\n"));
    fs.addFile("sd:/nl/themes/alpha/theme.ini", bytes("[light]\n"));
    fs.addFile("sd:/nl/themes/leer/background.png", bytes("x"));
    fs.addFile("sd:/nl/themes/datei.ini", bytes("x"));
    CHECK(listThemePacks(fs, "sd:/nl") == std::vector<std::string>{"alpha", "zeta"});

    test::MemFileSystem many;
    for (int i = 0; i < 40; ++i) many.addFile("sd:/nl/themes/p" + std::to_string(100 + i) + "/theme.ini", bytes(""));
    const auto packs = listThemePacks(many, "sd:/nl");
    CHECK(packs.size() == 32);
    CHECK(packs.front() == "p100");
}

TEST_CASE("listThemePacks: fehlender themes-Ordner → leer") {
    test::MemFileSystem fs;
    CHECK(listThemePacks(fs, "sd:/nl").empty());
}

TEST_CASE("config.ini: [theme] name (Spec v2 §16), alter Schlüssel pack als Rückfall, fehlend → leer") {
    CHECK(configFromIni(IniDoc::parse("[theme]\nmode = dark\n")).themePack.empty());
    CHECK(configFromIni(IniDoc::parse("[theme]\nname = smoke\n")).themePack == "smoke");
    CHECK(configFromIni(IniDoc::parse("[theme]\npack = smoke\n")).themePack == "smoke");
    CHECK(configFromIni(IniDoc::parse("[theme]\npack = alt\nname = neu\n")).themePack == "neu");
    CHECK(configFromIni(IniDoc::parse("[theme]\nname = ../x\n")).themePack.empty());  // kein Pfad
    Config c;
    c.themePack = "smoke";
    const std::string text = configToIniText(c, "[theme]\npack = smoke\n");
    CHECK(text.find("name = smoke") != std::string::npos);
    CHECK(text.find("pack") == std::string::npos);  // der alte Schlüssel wird ersetzt, nicht verdoppelt
}

TEST_CASE("isThemePackName: nur ein Ordnername ohne Pfadteile") {
    CHECK(isThemePackName("smoke"));
    CHECK_FALSE(isThemePackName("a/b"));
    CHECK_FALSE(isThemePackName("a\\b"));
    CHECK_FALSE(isThemePackName("sd:x"));
    CHECK_FALSE(isThemePackName(".."));
    CHECK_FALSE(isThemePackName(""));
    CHECK_FALSE(isThemePackName(std::string(65, 'x')));
}
