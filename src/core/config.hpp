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
    std::string debugCoverUrl;   // [debug] cover_url: ersetzt die GameTDB-Basis (Smoke-Test)
    bool debugFetchCovers = false;  // [debug] fetch_covers = 1: „Fehlende Cover laden“ nach NL_READY
};

Config configFromIni(const IniDoc& ini);
std::string defaultConfigText();
int parseClock(std::string_view hhmm, int fallback);  // "19:00" → 1140
std::string formatClock(int minuteOfDay);              // 1140 → "19:00"

// Schreibt alle Schlüssel aus Spec 5.4 in den bisherigen Dateitext. Kommentare, unbekannte Schlüssel,
// fremde Abschnitte (auch [debug]) und das Zeilenende (CRLF/LF) bleiben erhalten; fehlende Schlüssel
// kommen ans Ende ihres Abschnitts, fehlende Abschnitte ans Dateiende.
std::string configToIniText(const Config& c, std::string_view previousText);

}  // namespace nl
