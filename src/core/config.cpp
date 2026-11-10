#include "core/config.hpp"

namespace nl {

namespace {

// Ganzzahl im Bereich [lo, hi], sonst fallback.
int parseIntIn(std::string_view s, int lo, int hi, int fallback) {
    s = trim(s);
    if (s.empty() || s.size() > 6) return fallback;
    int v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return fallback;
        v = v * 10 + (c - '0');
    }
    return (v >= lo && v <= hi) ? v : fallback;
}

}  // namespace

int parseClock(std::string_view hhmm, int fallback) {
    hhmm = trim(hhmm);
    const std::size_t colon = hhmm.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon > 2) return fallback;
    const std::string_view m = hhmm.substr(colon + 1);
    if (m.empty() || m.size() > 2) return fallback;
    const int h = parseIntIn(hhmm.substr(0, colon), 0, 23, -1);
    const int mm = parseIntIn(m, 0, 59, -1);
    if (h < 0 || mm < 0) return fallback;
    return h * 60 + mm;
}

Config configFromIni(const IniDoc& ini) {
    Config c;
    const std::string_view lang = ini.get("general", "language");
    if (lang == "de") c.language = Config::Language::De;
    else if (lang == "en") c.language = Config::Language::En;

    if (ini.get("general", "library") == "sd") c.library = Config::LibraryDevice::Sd;
    c.usbTimeoutSec = parseIntIn(ini.get("general", "usb_timeout"), 1, 120, c.usbTimeoutSec);

    const std::string_view mode = ini.get("theme", "mode");
    if (mode == "dark") c.themeMode = ThemeMode::Dark;
    else if (mode == "auto") c.themeMode = ThemeMode::Auto;
    c.darkFromMin = parseClock(ini.get("theme", "dark_from"), c.darkFromMin);
    c.darkToMin = parseClock(ini.get("theme", "dark_to"), c.darkToMin);

    c.music = parseIntIn(ini.get("audio", "music"), 0, 1, 1) == 1;
    c.musicVolume = parseIntIn(ini.get("audio", "music_volume"), 0, 100, c.musicVolume);
    c.sounds = parseIntIn(ini.get("audio", "sounds"), 0, 1, 1) == 1;

    const std::string_view region = ini.get("covers", "region");
    for (std::string_view allowed : {"auto", "EN", "DE", "FR", "US", "JA"}) {
        if (region == allowed) c.coverRegion = std::string(region);
    }
    return c;
}

std::string defaultConfigText() {
    return "[general]\n"
           "language = auto\n"
           "library = usb\n"
           "usb_timeout = 15\n"
           "[theme]\n"
           "mode = light\n"
           "dark_from = 19:00\n"
           "dark_to = 07:00\n"
           "[audio]\n"
           "music = 1\n"
           "music_volume = 60\n"
           "sounds = 1\n"
           "[covers]\n"
           "region = auto\n";
}

}  // namespace nl
