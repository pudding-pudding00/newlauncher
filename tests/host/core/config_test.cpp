#include "core/config.hpp"
#include <doctest/doctest.h>
using namespace nl;

TEST_CASE("configFromIni: Standardwerte bei leerer Datei") {
    const Config c = configFromIni(IniDoc::parse(""));
    CHECK(c.language == Config::Language::Auto);
    CHECK(c.library == Config::LibraryDevice::Usb);
    CHECK(c.usbTimeoutSec == 15);
    CHECK(c.themeMode == ThemeMode::Light);
    CHECK(c.darkFromMin == 1140);
    CHECK(c.darkToMin == 420);
    CHECK(c.music);
    CHECK(c.musicVolume == 60);
    CHECK(c.sounds);
    CHECK(c.coverRegion == "auto");
}

TEST_CASE("configFromIni: defaultConfigText ergibt die Standardwerte") {
    const Config c = configFromIni(IniDoc::parse(defaultConfigText()));
    const Config d;
    CHECK(c.language == d.language);
    CHECK(c.library == d.library);
    CHECK(c.usbTimeoutSec == d.usbTimeoutSec);
    CHECK(c.themeMode == d.themeMode);
    CHECK(c.darkFromMin == d.darkFromMin);
    CHECK(c.darkToMin == d.darkToMin);
    CHECK(c.musicVolume == d.musicVolume);
    CHECK(c.coverRegion == d.coverRegion);
    CHECK(defaultConfigText().find("[general]\nlanguage = auto\nlibrary = usb\nusb_timeout = 15\n") == 0);
}

TEST_CASE("configFromIni: gesetzte Werte") {
    const Config c = configFromIni(IniDoc::parse(
        "[general]\nlanguage = de\nlibrary = sd\nusb_timeout = 30\n[theme]\nmode = auto\ndark_from = 20:30\n"
        "dark_to = 6:00\n[audio]\nmusic = 0\nmusic_volume = 80\nsounds = 0\n[covers]\nregion = DE\n"));
    CHECK(c.language == Config::Language::De);
    CHECK(c.library == Config::LibraryDevice::Sd);
    CHECK(c.usbTimeoutSec == 30);
    CHECK(c.themeMode == ThemeMode::Auto);
    CHECK(c.darkFromMin == 20 * 60 + 30);
    CHECK(c.darkToMin == 360);
    CHECK_FALSE(c.music);
    CHECK(c.musicVolume == 80);
    CHECK_FALSE(c.sounds);
    CHECK(c.coverRegion == "DE");
}

TEST_CASE("configFromIni: ungültige Werte fallen auf Standard") {
    const Config c = configFromIni(IniDoc::parse(
        "[general]\nlanguage = fr\nlibrary = nas\nusb_timeout = -3\n[theme]\nmode = blau\ndark_from = 25:00\n"
        "[audio]\nmusic_volume = 150\n[covers]\nregion = XX\n"));
    CHECK(c.language == Config::Language::Auto);
    CHECK(c.library == Config::LibraryDevice::Usb);
    CHECK(c.usbTimeoutSec == 15);
    CHECK(c.themeMode == ThemeMode::Light);
    CHECK(c.darkFromMin == 1140);
    CHECK(c.musicVolume == 60);
    CHECK(c.coverRegion == "auto");
}

TEST_CASE("configFromIni: [debug] autostart wird gelesen, fehlt → leer") {
    CHECK(nl::configFromIni(nl::IniDoc::parse("")).debugAutostart.empty());
    const auto c = nl::configFromIni(nl::IniDoc::parse("[debug]\nautostart = nes/Zelda (Europe) (Rev 1).nes\n"));
    CHECK(c.debugAutostart == "nes/Zelda (Europe) (Rev 1).nes");
}

TEST_CASE("parseClock") {
    CHECK(parseClock("07:00", -1) == 420);
    CHECK(parseClock("7:5", -1) == 425);
    CHECK(parseClock("23:59", -1) == 1439);
    CHECK(parseClock("24:00", -1) == -1);
    CHECK(parseClock("12:60", -1) == -1);
    CHECK(parseClock("ab", -1) == -1);
    CHECK(parseClock("", -1) == -1);
    CHECK(parseClock("123:00", -1) == -1);
    CHECK(parseClock(" 8:15 ", -1) == 495);
}
