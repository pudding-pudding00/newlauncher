#pragma once
// Zeilen der Einstellungsszene (Spec 3.4) und wie A/◀▶ sie ändern. Plattformfrei, host-getestet.

#include "core/config.hpp"
#include "core/types.hpp"

namespace nl {

enum class SettingRow : u8 {
    Language,
    Theme,
    DarkFrom,
    DarkTo,
    Music,
    Volume,
    Sounds,
    Library,
    CoverRegion,
    Rescan,
    FetchCovers,
    VcMap,
    Count
};

bool isAction(SettingRow r);  // Rescan, FetchCovers, VcMap: lösen etwas aus statt einen Wert zu ändern
// dir = ±1. Uhrzeiten ±30 min mit Umlauf, Lautstärke ±10 begrenzt auf 0..100, sonst zyklisch.
void stepSetting(Config& c, SettingRow r, int dir);
// DarkFrom/DarkTo nur bei ThemeMode::Auto, Volume nur bei eingeschalteter Musik.
bool settingEnabled(const Config& c, SettingRow r);

}  // namespace nl
