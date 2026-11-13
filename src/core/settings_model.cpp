#include "core/settings_model.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace nl {

namespace {

constexpr int kDay = 24 * 60;
constexpr std::array<std::string_view, 6> kRegions = {"auto", "EN", "DE", "FR", "US", "JA"};

template <typename E>
E cycle(E v, int count, int dir) {
    const int i = (static_cast<int>(v) + (dir < 0 ? count - 1 : 1)) % count;
    return static_cast<E>(i);
}

int stepClock(int minute, int dir) { return ((minute + dir * 30) % kDay + kDay) % kDay; }

}  // namespace

void stepThemePack(Config& c, std::span<const std::string> packs, int dir) {
    const int n = static_cast<int>(packs.size()) + 1;  // 0 = Standard
    int i = 0;
    for (std::size_t k = 0; k < packs.size(); ++k)
        if (packs[k] == c.themePack) i = static_cast<int>(k) + 1;
    if (i == 0 && !c.themePack.empty()) i = dir < 0 ? 1 : n - 1;  // unbekannt: wie Standard weiterzählen
    const int next = (i + (dir < 0 ? n - 1 : 1)) % n;
    c.themePack = next == 0 ? std::string() : packs[static_cast<std::size_t>(next - 1)];
}

bool isAction(SettingRow r) {
    return r == SettingRow::Rescan || r == SettingRow::FetchCovers || r == SettingRow::VcMap ||
           r == SettingRow::Emulators || r == SettingRow::WiiGames || r == SettingRow::GcGames || r == SettingRow::Parental || r == SettingRow::Setup || r == SettingRow::SysInfo ||
           r == SettingRow::About;
}

void stepSetting(Config& c, SettingRow r, int dir) {
    dir = dir < 0 ? -1 : 1;
    switch (r) {
        case SettingRow::Language: c.language = cycle(c.language, 3, dir); break;
        case SettingRow::Theme: c.themeMode = cycle(c.themeMode, 3, dir); break;
        case SettingRow::DarkFrom: c.darkFromMin = stepClock(c.darkFromMin, dir); break;
        case SettingRow::DarkTo: c.darkToMin = stepClock(c.darkToMin, dir); break;
        case SettingRow::Music: c.music = !c.music; break;
        case SettingRow::Volume: c.musicVolume = std::clamp(c.musicVolume + dir * 10, 0, 100); break;
        case SettingRow::Sounds: c.sounds = !c.sounds; break;
        case SettingRow::Sort: c.sortOrder = cycle(c.sortOrder, 4, dir); break;
        case SettingRow::Library:
        {
            // Umlauf Auto → USB → SD; eine eigene Liste aus config.ini steht vor Auto.
            const SourceChoice cur = sourceChoice(c.libraryRules.sources);
            const int i = cur == SourceChoice::Custom ? -1 : static_cast<int>(cur);
            const int next = cur == SourceChoice::Custom && dir < 0 ? 2 : (i + dir + 3) % 3;
            if (next == 0) c.libraryRules.sources = SourceSpec{};
            else c.libraryRules.sources = SourceSpec{false, {next == 1 ? "usb" : "sd"}};
            c.library = next == 2 ? Config::LibraryDevice::Sd : Config::LibraryDevice::Usb;  // Alt-Schlüssel folgt
            break;
        }
        case SettingRow::CoverRegion: {
            const auto it = std::find(kRegions.begin(), kRegions.end(), std::string_view(c.coverRegion));
            const int n = static_cast<int>(kRegions.size());
            const int i = it == kRegions.end() ? 0 : static_cast<int>(it - kRegions.begin());
            c.coverRegion = std::string(kRegions[static_cast<std::size_t>((i + dir + n) % n)]);
            break;
        }
        case SettingRow::ThemePack:  // braucht die Paketliste: stepThemePack
        case SettingRow::Rescan:
        case SettingRow::FetchCovers:
        case SettingRow::VcMap:
        case SettingRow::Emulators:
        case SettingRow::WiiGames:
        case SettingRow::GcGames:
        case SettingRow::Parental:
        case SettingRow::Setup:
        case SettingRow::SysInfo:
        case SettingRow::About:
        case SettingRow::Count: break;
    }
}

bool settingEnabled(const Config& c, SettingRow r) {
    switch (r) {
        case SettingRow::DarkFrom:
        case SettingRow::DarkTo: return c.themeMode == ThemeMode::Auto;
        case SettingRow::Volume: return c.music;
        default: return true;
    }
}

}  // namespace nl
