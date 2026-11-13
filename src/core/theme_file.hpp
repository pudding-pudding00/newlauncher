#pragma once
// Theme-Pakete aus <data>/themes/<name>/theme.ini (Spec v2 10.2). Plattformfrei, host-getestet.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/fs.hpp"
#include "core/ini.hpp"
#include "core/theme.hpp"

namespace nl {

struct ThemePair {
    Theme light = lightTheme(), dark = darkTheme();
};

inline constexpr std::size_t kThemeTokenCount = 14;
inline constexpr std::size_t kMaxThemePacks = 32;
// Namen in theme.ini, in der Reihenfolge der Felder von Theme.
extern const char* const kThemeTokenNames[kThemeTokenCount];

Color& themeToken(Theme& t, std::size_t i);
const Color& themeToken(const Theme& t, std::size_t i);

// "#RRGGBB" oder "#RRGGBBAA", Groß/klein egal, außen getrimmt; alles andere → nullopt.
std::optional<Color> parseHexColor(std::string_view s);
// [light] und [dark]; fehlende Tokens bleiben Standard, kaputte auch und landen als "light.bar_border" in bad.
ThemePair themeFromIni(const IniDoc& ini, std::vector<std::string>* bad);
// Unterordner von <dataDir>/themes mit theme.ini, sortiert, höchstens kMaxThemePacks.
std::vector<std::string> listThemePacks(FileSystem& fs, std::string_view dataDir);
// Gültiger Paketname: ein Ordnername ohne Pfadteile (kein /, \, :, ..), 1–64 Zeichen.
bool isThemePackName(std::string_view name);

}  // namespace nl
