#pragma once
// config.ini (Spec 3.4/5.4): Einstellungen mit Standardwerten; ungültige Werte fallen auf Standard.

#include <string>
#include <string_view>

#include "core/ini.hpp"
#include "core/theme.hpp"

namespace nl {

struct Config {
    enum class Language : u8 { Auto, De, En };
    enum class LibraryDevice : u8 { Usb, Sd };

    Language language = Language::Auto;
    LibraryDevice library = LibraryDevice::Usb;
    int usbTimeoutSec = 15;
    ThemeMode themeMode = ThemeMode::Light;
    int darkFromMin = 19 * 60, darkToMin = 7 * 60;
    bool music = true;
    int musicVolume = 60;
    bool sounds = true;
    std::string coverRegion = "auto";
    std::string debugAutostart;  // [debug] autostart: Spielschlüssel, der nach NL_READY startet
};

Config configFromIni(const IniDoc& ini);
std::string defaultConfigText();
int parseClock(std::string_view hhmm, int fallback);  // "19:00" → 1140

}  // namespace nl
