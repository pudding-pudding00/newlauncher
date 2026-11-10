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
inline constexpr const char* kStrSearchHint = "Titel eingeben …";
inline constexpr const char* kStrNoResults = "Keine Treffer";
inline constexpr const char* kStrKeySpace = "Leerzeichen";
inline constexpr const char* kStrKeyDelete = "Löschen";
inline constexpr const char* kStrKeyDone = "Fertig";
inline constexpr const char* kStrLastPlayed = "zuletzt";
inline constexpr const char* kStrStart = "Starten";
inline constexpr const char* kStrFavorite = "Favorit";
inline constexpr const char* kStrFavRemove = "Favorit entfernen";
inline constexpr const char* kStrDetailsHelp = "A Starten · B Zurück · 1 Favorit";
inline constexpr const char* kStrFavAdded = "Zu Favoriten hinzugefügt";
inline constexpr const char* kStrFavRemoved = "Aus Favoriten entfernt";
inline constexpr const char* kStrLaunchFailed = "Start nicht möglich";
inline constexpr const char* kStrEmuMissing = "Emulator nicht gefunden:\n%s";
inline constexpr const char* kStrEmuInvalid = "Emulator-Datei ist ungültig:\n%s";
inline constexpr const char* kStrRomMissing = "Spieldatei nicht gefunden:\n%s";
inline constexpr const char* kStrUnsupported = "Starten für %s folgt in einer späteren Version.";
inline constexpr const char* kStrArgsTooLong = "Pfad zu lang für den Emulator.";
inline constexpr const char* kStrOk = "OK";
inline constexpr const char* kStrNoN64Path = "Kein N64-Startweg verfügbar.\nVC-Inject zuordnen oder Not64/Wii64 auf die SD legen.";
inline constexpr const char* kStrMethod = "Startmethode: %s";
inline constexpr const char* kStrMethodAuto = "Automatisch (→ %s)";
inline constexpr const char* kStrMethodAutoNone = "Automatisch (kein Weg)";
inline constexpr const char* kStrWhyNotMapped = "nicht zugeordnet";
inline constexpr const char* kStrWhyNotInstalled = "Kanal nicht installiert";
inline constexpr const char* kStrWhyDolMissing = "DOL fehlt";
inline constexpr const char* kStrOverrideIgnored = "Gewählter Weg nicht verfügbar – automatisch gewählt.";
inline constexpr const char* kStrVcMapTitle = "VC-Injects zuordnen";
inline constexpr const char* kStrVcNone = "—";
inline constexpr const char* kStrVcSuggest = "Vorschlag: %s (A übernimmt)";
inline constexpr const char* kStrVcNotInstalled = "%s (nicht installiert)";
inline constexpr const char* kStrVcNoChannels = "Keine Kanäle installiert.";
inline constexpr const char* kStrVcNoGames = "Keine N64-Spiele in der Bibliothek.";
inline constexpr const char* kStrVcMapHelp = "A Vorschlag · ◀▶ Kanal · B Speichern und zurück";

inline constexpr const char* kStrWeekdays[7] = {"Montag",  "Dienstag", "Mittwoch", "Donnerstag",
                                                 "Freitag", "Samstag",  "Sonntag"};
inline constexpr const char* kStrMonths[12] = {"Januar", "Februar",   "März",    "April",
                                               "Mai",    "Juni",      "Juli",    "August",
                                               "September", "Oktober", "November", "Dezember"};

}  // namespace nl::ui
