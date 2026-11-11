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

bool isAction(SettingRow r) {
    return r == SettingRow::Rescan || r == SettingRow::FetchCovers || r == SettingRow::VcMap ||
           r == SettingRow::Emulators || r == SettingRow::About;
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
        case SettingRow::Library: c.library = cycle(c.library, 2, dir); break;
        case SettingRow::CoverRegion: {
            const auto it = std::find(kRegions.begin(), kRegions.end(), std::string_view(c.coverRegion));
            const int n = static_cast<int>(kRegions.size());
            const int i = it == kRegions.end() ? 0 : static_cast<int>(it - kRegions.begin());
            c.coverRegion = std::string(kRegions[static_cast<std::size_t>((i + dir + n) % n)]);
            break;
        }
        case SettingRow::Rescan:
        case SettingRow::FetchCovers:
        case SettingRow::VcMap:
        case SettingRow::Emulators:
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
