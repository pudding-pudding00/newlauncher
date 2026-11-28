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
    Banners,  // [ui] banners (M18)
    Library,
    Sort,  // Sortierung der Konsolenleisten (M17)
    CoverRegion,
    CoverAuto,
    CoverStorage,  // [covers] storage (M24)  // [covers] auto (M21)
    Rescan,
    FetchCovers,
    ClearCovers,  // M23: Cover-Cache löschen (zweimal A)
    VcMap,
    Emulators,
    WiiGames,  // globale Spieloptionen Wii (M14)
    GcGames,   // globale Spieloptionen GameCube (M14)
    Cloud,     // Cloud-Spielstände (M27): Untermenü
    Parental,  // Kindersicherung (M17)
    Transfer,  // File Transfer (FTP, M32): action opening TransferScene
    Setup,    // Einrichtungs-Assistent (M13)
    SysInfo,  // Systeminfo (M13)
    About,  // Version, Lizenz, Drittanbieter (M11)
    SelfUpdate,  // Update aus dem GitHub-Release (M20)
    Restart,     // newlauncher neu starten (M20)
    Count
};

bool isAction(SettingRow r);  // Rescan, FetchCovers, ClearCovers, VcMap, Emulators, WiiGames, GcGames, Cloud, SysInfo, About, SelfUpdate, Restart: lösen etwas aus statt einen Wert zu ändern
// dir = ±1. Uhrzeiten ±30 min mit Umlauf, Lautstärke ±10 begrenzt auf 0..100, sonst zyklisch.
void stepSetting(Config& c, SettingRow r, int dir);
// DarkFrom/DarkTo nur bei ThemeMode::Auto, Volume nur bei eingeschalteter Musik.
bool settingEnabled(const Config& c, SettingRow r);
// ThemePack: Standard (leer) → packs[0] → … → Standard; ein unbekanntes Paket gilt als Standard.
void stepThemePack(Config& c, std::span<const std::string> packs, int dir);

}  // namespace nl
