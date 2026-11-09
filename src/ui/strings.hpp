#pragma once
// Sichtbare UI-Texte, zentral an einer Stelle. In M1 nur Deutsch; DE/EN folgt in M6 (Spec 3.4).

namespace nl::ui {

inline constexpr const char* kStrFavorites = "Favoriten";
inline constexpr const char* kStrRecent = "Zuletzt";
inline constexpr const char* kStrHomeMenu = "HOME-Menü";
inline constexpr const char* kStrHomebrewChannel = "Homebrew Channel";
inline constexpr const char* kStrWiiMenu = "Wii-Menü";
inline constexpr const char* kStrSettings = "Einstellungen";
inline constexpr const char* kStrPowerOff = "Ausschalten";
inline constexpr const char* kStrHomeCloses = "HOME schließt";
inline constexpr const char* kStrNoGames = "Keine Spiele";
inline constexpr const char* kStrAppearance = "Darstellung";
inline constexpr const char* kStrPlayer = "Spieler";
inline constexpr const char* kStrWaitUsb = "Warte auf USB …";
inline constexpr const char* kStrNoUsb = "Kein USB-Gerät gefunden";
inline constexpr const char* kStrRetry = "Erneut versuchen";
inline constexpr const char* kStrUseSd = "SD verwenden";
inline constexpr const char* kStrScanning = "Bibliothek wird durchsucht …";
inline constexpr const char* kStrFiles = "Dateien";
inline constexpr const char* kStrSearch = "Suche";
inline constexpr const char* kStrFavAdded = "Zu Favoriten hinzugefügt";
inline constexpr const char* kStrFavRemoved = "Aus Favoriten entfernt";

inline constexpr const char* kStrWeekdays[7] = {"Montag",  "Dienstag", "Mittwoch", "Donnerstag",
                                                 "Freitag", "Samstag",  "Sonntag"};
inline constexpr const char* kStrMonths[12] = {"Januar", "Februar",   "März",    "April",
                                               "Mai",    "Juni",      "Juli",    "August",
                                               "September", "Oktober", "November", "Dezember"};

}  // namespace nl::ui
