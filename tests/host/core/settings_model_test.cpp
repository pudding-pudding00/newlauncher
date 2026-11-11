#include "core/settings_model.hpp"

#include <doctest/doctest.h>

using namespace nl;

TEST_CASE("stepSetting: Sprache zyklisch") {
    Config c;
    stepSetting(c, SettingRow::Language, +1);
    CHECK(c.language == Config::Language::De);
    stepSetting(c, SettingRow::Language, +1);
    CHECK(c.language == Config::Language::En);
    stepSetting(c, SettingRow::Language, +1);
    CHECK(c.language == Config::Language::Auto);
    stepSetting(c, SettingRow::Language, -1);
    CHECK(c.language == Config::Language::En);
}

TEST_CASE("stepSetting: Darstellung zyklisch") {
    Config c;
    stepSetting(c, SettingRow::Theme, +1);
    CHECK(c.themeMode == ThemeMode::Dark);
    stepSetting(c, SettingRow::Theme, +1);
    CHECK(c.themeMode == ThemeMode::Auto);
    stepSetting(c, SettingRow::Theme, +1);
    CHECK(c.themeMode == ThemeMode::Light);
}

TEST_CASE("stepSetting: Lautstärke begrenzt") {
    Config c;
    c.musicVolume = 100;
    stepSetting(c, SettingRow::Volume, +1);
    CHECK(c.musicVolume == 100);
    c.musicVolume = 0;
    stepSetting(c, SettingRow::Volume, -1);
    CHECK(c.musicVolume == 0);
    stepSetting(c, SettingRow::Volume, +1);
    CHECK(c.musicVolume == 10);
}

TEST_CASE("stepSetting: Uhrzeit mit Umlauf") {
    Config c;
    c.darkToMin = 1410;
    stepSetting(c, SettingRow::DarkTo, +1);
    CHECK(c.darkToMin == 0);
    c.darkFromMin = 0;
    stepSetting(c, SettingRow::DarkFrom, -1);
    CHECK(c.darkFromMin == 1410);
}

TEST_CASE("stepSetting: Schalter, Gerät, Region") {
    Config c;
    stepSetting(c, SettingRow::Music, +1);
    CHECK_FALSE(c.music);
    stepSetting(c, SettingRow::Sounds, -1);
    CHECK_FALSE(c.sounds);
    stepSetting(c, SettingRow::Library, +1);
    CHECK(c.library == Config::LibraryDevice::Sd);
    stepSetting(c, SettingRow::Library, +1);
    CHECK(c.library == Config::LibraryDevice::Usb);
    c.coverRegion = "JA";
    stepSetting(c, SettingRow::CoverRegion, +1);
    CHECK(c.coverRegion == "auto");
    stepSetting(c, SettingRow::CoverRegion, -1);
    CHECK(c.coverRegion == "JA");
    c.coverRegion = "kaputt";
    stepSetting(c, SettingRow::CoverRegion, +1);
    CHECK(c.coverRegion == "EN");
}

TEST_CASE("Aktionszeilen ändern nichts") {
    Config c;
    const Config before = c;
    for (SettingRow r : {SettingRow::Rescan, SettingRow::FetchCovers, SettingRow::VcMap, SettingRow::Emulators,
                          SettingRow::About}) {
        CHECK(isAction(r));
        stepSetting(c, r, +1);
    }
    CHECK(c.language == before.language);
    CHECK(c.musicVolume == before.musicVolume);
    CHECK_FALSE(isAction(SettingRow::Volume));
}

TEST_CASE("settingEnabled") {
    Config c;
    CHECK_FALSE(settingEnabled(c, SettingRow::DarkFrom));
    CHECK_FALSE(settingEnabled(c, SettingRow::DarkTo));
    c.themeMode = ThemeMode::Auto;
    CHECK(settingEnabled(c, SettingRow::DarkFrom));
    CHECK(settingEnabled(c, SettingRow::Volume));
    c.music = false;
    CHECK_FALSE(settingEnabled(c, SettingRow::Volume));
    CHECK(settingEnabled(c, SettingRow::Language));
}

TEST_CASE("Zeile Über ist die letzte und eine Aktion (M11)") {
    CHECK(static_cast<int>(SettingRow::About) + 1 == static_cast<int>(SettingRow::Count));
    CHECK(isAction(SettingRow::About));
}
