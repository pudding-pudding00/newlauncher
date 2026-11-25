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
    // M12-Review: Auto → USB → SD → Auto; die Quellenliste ist der Wert der Zeile.
    REQUIRE(c.libraryRules.sources.autoMode);
    stepSetting(c, SettingRow::Library, +1);
    CHECK(sourceChoice(c.libraryRules.sources) == SourceChoice::Usb);
    CHECK(c.libraryRules.sources.devices == std::vector<std::string>{"usb"});
    CHECK(c.library == Config::LibraryDevice::Usb);
    stepSetting(c, SettingRow::Library, +1);
    CHECK(sourceChoice(c.libraryRules.sources) == SourceChoice::Sd);
    CHECK(c.library == Config::LibraryDevice::Sd);  // Alt-Schlüssel folgt
    stepSetting(c, SettingRow::Library, +1);
    CHECK(c.libraryRules.sources.autoMode);
    stepSetting(c, SettingRow::Library, -1);
    CHECK(sourceChoice(c.libraryRules.sources) == SourceChoice::Sd);
    c.libraryRules.sources = SourceSpec{false, {"usb2", "sd"}};
    CHECK(sourceChoice(c.libraryRules.sources) == SourceChoice::Custom);
    stepSetting(c, SettingRow::Library, +1);
    CHECK(c.libraryRules.sources.autoMode);  // eigene Liste: ein Druck führt zurück in den Umlauf
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
    for (SettingRow r : {SettingRow::Rescan, SettingRow::FetchCovers, SettingRow::ClearCovers, SettingRow::VcMap, SettingRow::Emulators,
                          SettingRow::Setup, SettingRow::SysInfo, SettingRow::About}) {
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

TEST_CASE("Zeile Über ist eine Aktion (M11), danach Update und Neustart (M20)") {
    CHECK(static_cast<int>(SettingRow::About) + 1 == static_cast<int>(SettingRow::SelfUpdate));
    CHECK(static_cast<int>(SettingRow::SelfUpdate) + 1 == static_cast<int>(SettingRow::Restart));
    CHECK(static_cast<int>(SettingRow::Restart) + 1 == static_cast<int>(SettingRow::Count));
    CHECK(isAction(SettingRow::About));
    CHECK(isAction(SettingRow::SelfUpdate));
    CHECK(isAction(SettingRow::Restart));
}

TEST_CASE("Zeile Systeminfo steht vor Über und ist eine Aktion (M13)") {
    CHECK(static_cast<int>(SettingRow::SysInfo) + 1 == static_cast<int>(SettingRow::About));
    CHECK(isAction(SettingRow::SysInfo));
}

TEST_CASE("Zeile Einrichtung steht vor Systeminfo und ist eine Aktion (M13 Task 5)") {
    CHECK(static_cast<int>(SettingRow::Setup) + 1 == static_cast<int>(SettingRow::SysInfo));
    CHECK(isAction(SettingRow::Setup));
}

TEST_CASE("stepThemePack: Standard → Pakete → Standard, rückwärts auch") {
    Config c;
    const std::vector<std::string> packs{"alpha", "zeta"};
    stepThemePack(c, packs, +1);
    CHECK(c.themePack == "alpha");
    stepThemePack(c, packs, +1);
    CHECK(c.themePack == "zeta");
    stepThemePack(c, packs, +1);
    CHECK(c.themePack.empty());
    stepThemePack(c, packs, -1);
    CHECK(c.themePack == "zeta");
}

TEST_CASE("stepThemePack: unbekanntes Paket aus config.ini springt auf den Anfang") {
    Config c;
    c.themePack = "weg";
    stepThemePack(c, std::vector<std::string>{"alpha"}, +1);
    CHECK(c.themePack.empty());
    stepThemePack(c, std::vector<std::string>{}, +1);
    CHECK(c.themePack.empty());
    CHECK(settingEnabled(c, SettingRow::ThemePack));
    CHECK_FALSE(isAction(SettingRow::ThemePack));
}

TEST_CASE("stepSetting: Sortierung zyklisch (M17)") {
    Config c;
    stepSetting(c, SettingRow::Sort, +1);
    CHECK(c.sortOrder == SortOrder::Year);
    stepSetting(c, SettingRow::Sort, +1);
    CHECK(c.sortOrder == SortOrder::Recent);
    stepSetting(c, SettingRow::Sort, +1);
    CHECK(c.sortOrder == SortOrder::Most);
    stepSetting(c, SettingRow::Sort, +1);
    CHECK(c.sortOrder == SortOrder::Title);
    stepSetting(c, SettingRow::Sort, -1);
    CHECK(c.sortOrder == SortOrder::Most);
    CHECK_FALSE(isAction(SettingRow::Sort));
}

TEST_CASE("Zeile Banner folgt auf Klänge und schaltet [ui] banners (M18)") {
    CHECK(static_cast<int>(SettingRow::Sounds) + 1 == static_cast<int>(SettingRow::Banners));
    CHECK_FALSE(isAction(SettingRow::Banners));
    Config c;
    CHECK(settingEnabled(c, SettingRow::Banners));
    stepSetting(c, SettingRow::Banners, 1);
    CHECK_FALSE(c.banners);
    stepSetting(c, SettingRow::Banners, -1);
    CHECK(c.banners);
}

TEST_CASE("Zeile Cover automatisch folgt auf Cover-Region und schaltet [covers] auto (M21)") {
    CHECK(static_cast<int>(SettingRow::CoverRegion) + 1 == static_cast<int>(SettingRow::CoverAuto));
    CHECK_FALSE(isAction(SettingRow::CoverAuto));
    Config c;
    CHECK(settingEnabled(c, SettingRow::CoverAuto));
    stepSetting(c, SettingRow::CoverAuto, 1);
    CHECK_FALSE(c.coverAuto);
    stepSetting(c, SettingRow::CoverAuto, -1);
    CHECK(c.coverAuto);
}
