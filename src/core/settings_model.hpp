#pragma once
// Zeilen der Einstellungsszene (Spec 3.4) und wie A/◀▶ sie ändern. Plattformfrei, host-getestet.

#include "core/config.hpp"
#include "core/types.hpp"

#include <span>
#include <string>

namespace nl {

enum class SettingRow : u8 {
    Language,
    Theme,
    DarkFrom,
    DarkTo,
    ThemePack,  // Theme-Paket aus <data>/themes (M17)
    Music,
    Volume,
    Sounds,
    Library,
    Sort,  // Sortierung der Konsolenleisten (M17)
    CoverRegion,
    Rescan,
    FetchCovers,
    VcMap,
    Emulators,
    WiiGames,  // globale Spieloptionen Wii (M14)
    GcGames,   // globale Spieloptionen GameCube (M14)
    Setup,    // Einrichtungs-Assistent (M13)
    SysInfo,  // Systeminfo (M13)
    About,  // Version, Lizenz, Drittanbieter (M11)
    Count
};

bool isAction(SettingRow r);  // Rescan, FetchCovers, VcMap, Emulators, WiiGames, GcGames, SysInfo, About: lösen etwas aus statt einen Wert zu ändern
// dir = ±1. Uhrzeiten ±30 min mit Umlauf, Lautstärke ±10 begrenzt auf 0..100, sonst zyklisch.
void stepSetting(Config& c, SettingRow r, int dir);
// DarkFrom/DarkTo nur bei ThemeMode::Auto, Volume nur bei eingeschalteter Musik.
bool settingEnabled(const Config& c, SettingRow r);
// ThemePack: Standard (leer) → packs[0] → … → Standard; ein unbekanntes Paket gilt als Standard.
void stepThemePack(Config& c, std::span<const std::string> packs, int dir);

}  // namespace nl
