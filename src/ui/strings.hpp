#pragma once
// Sichtbare UI-Texte in Deutsch und Englisch (Spec 4.1). Jede Zeile der X-Makro-Tabelle ist
// X(Id, "deutsch", "english"); Texte mit Formatzeichen haben in beiden Sprachen dieselbe Folge
// (strings_test prüft das). Die aktive Sprache ist globaler UI-Zustand, gesetzt von main.

#include "core/config.hpp"
#include "core/types.hpp"

// clang-format off
#define NL_STRINGS(X) \
    X(Favorites, "Favoriten", "Favorites") \
    X(Recent, "Zuletzt", "Recent") \
    X(HomeMenu, "HOME-Menü", "HOME Menu") \
    X(HomebrewChannel, "Homebrew Channel", "Homebrew Channel") \
    X(WiiMenu, "Wii-Menü", "Wii Menu") \
    X(Settings, "Einstellungen", "Settings") \
    X(PowerOff, "Ausschalten", "Power off") \
    X(HomeCloses, "HOME schließt", "HOME closes") \
    X(NoGames, "Keine Spiele", "No games") \
    X(Appearance, "Darstellung", "Appearance") \
    X(ThemeLight, "Hell", "Light") \
    X(ThemeDark, "Dunkel", "Dark") \
    X(ThemeAuto, "Automatisch", "Automatic") \
    X(Player, "Spieler", "players") \
    X(WaitUsb, "Warte auf USB …", "Waiting for USB …") \
    X(NoUsb, "Kein USB-Gerät gefunden", "No USB device found") \
    X(Retry, "Erneut versuchen", "Retry") \
    X(UseSd, "SD verwenden", "Use SD") \
    X(Scanning, "Bibliothek wird durchsucht …", "Scanning library …") \
    X(Files, "Dateien", "files") \
    X(Search, "Suche", "Search") \
    X(SearchHint, "Titel eingeben …", "Enter a title …") \
    X(NoResults, "Keine Treffer", "No results") \
    X(KeySpace, "Leerzeichen", "Space") \
    X(KeyDelete, "Löschen", "Delete") \
    X(KeyDone, "Fertig", "Done") \
    X(LastPlayed, "zuletzt", "recent") \
    X(Start, "Starten", "Start") \
    X(Favorite, "Favorit", "Favorite") \
    X(FavRemove, "Favorit entfernen", "Remove favorite") \
    X(DetailsHelp, "A Starten · B Zurück · 1 Favorit", "A Start · B Back · 1 Favorite") \
    X(FavAdded, "Zu Favoriten hinzugefügt", "Added to favorites") \
    X(FavRemoved, "Aus Favoriten entfernt", "Removed from favorites") \
    X(LaunchFailed, "Start nicht möglich", "Cannot start") \
    X(EmuMissing, "Emulator nicht gefunden:\n%s", "Emulator not found:\n%s") \
    X(EmuInvalid, "Emulator-Datei ist ungültig:\n%s", "Emulator file is invalid:\n%s") \
    X(RomMissing, "Spieldatei nicht gefunden:\n%s", "Game file not found:\n%s") \
    X(Unsupported, "Starten für %s folgt in einer späteren Version.", "Starting %s comes in a later version.") \
    X(ArgsTooLong, "Pfad zu lang für den Emulator.", "Path too long for the emulator.") \
    X(NoMemory, "Speicher für den Start ist belegt.", "Memory for starting is in use.") \
    X(Ok, "OK", "OK") \
    X(NoN64Path, "Kein N64-Startweg verfügbar.\nVC-Inject zuordnen oder Not64/Wii64 auf die SD legen.", \
      "No way to start N64 games.\nMap a VC inject or put Not64/Wii64 on the SD card.") \
    X(Method, "Startmethode: %s", "Start method: %s") \
    X(MethodAuto, "Automatisch (→ %s)", "Automatic (→ %s)") \
    X(MethodAutoNone, "Automatisch (kein Weg)", "Automatic (none available)") \
    X(WhyNotMapped, "nicht zugeordnet", "not mapped") \
    X(WhyNotInstalled, "Kanal nicht installiert", "channel not installed") \
    X(WhyDolMissing, "DOL fehlt", "DOL missing") \
    X(NinMissing, "Nintendont fehlt.\nNicht gefunden: %s", "Nintendont missing.\nNot found: %s") \
    X(NinUnknown, "Nintendont nicht erkannt.\n%s enthält keine Versionsangabe.", \
      "Nintendont not recognized.\n%s contains no version string.") \
    X(NinTooOld, "Nintendont zu alt.\nGefunden %s, nötig 6.493 oder neuer.", "Nintendont too old.\nFound %s, need 6.493 or newer.") \
    X(NinPathTooLong, "Pfad zu lang.\nNintendont erlaubt höchstens 254 Zeichen: %s", \
      "Path too long.\nNintendont allows at most 254 characters: %s") \
    X(AhbProt, "Kein Hardwarezugriff.\nBitte den Launcher über den Homebrew Channel starten.", \
      "No hardware access.\nPlease start the launcher from the Homebrew Channel.") \
    X(OverrideIgnored,"Gewählter Weg nicht verfügbar – automatisch gewählt.", "Chosen method unavailable – picked automatically.") \
    X(VcMapTitle, "VC-Injects zuordnen", "Map VC injects") \
    X(VcNone, "—", "—") \
    X(VcSuggest, "Vorschlag: %s (A übernimmt)", "Suggestion: %s (A accepts)") \
    X(VcNotInstalled, "%s (nicht installiert)", "%s (not installed)") \
    X(VcNoChannels, "Keine Kanäle installiert.", "No channels installed.") \
    X(VcNoGames, "Keine N64-Spiele in der Bibliothek.", "No N64 games in the library.") \
    X(FetchCovers, "Fehlende Cover laden", "Download missing covers") \
    X(CoverProgress, "Cover: %u/%u", "Covers: %u/%u") \
    X(NoMissingCovers, "Keine fehlenden Cover", "No missing covers") \
    X(NoMissingCoversText, "Alle Wii- und GameCube-Spiele haben schon ein Cover.", "All Wii and GameCube games already have a cover.") \
    X(CoversLoaded, "Cover geladen", "Covers downloaded") \
    X(CoversSummary, "%u geladen, %u nicht gefunden, %u fehlgeschlagen", "%u downloaded, %u not found, %u failed") \
    X(VcMapHelp, "A Vorschlag · ◀▶ Kanal · B Speichern und zurück", "A Suggestion · ◀▶ Channel · B Save and back") \
    X(Language, "Sprache", "Language") \
    X(LangAuto, "Automatisch", "Automatic") \
    X(LangDe, "Deutsch", "Deutsch") \
    X(LangEn, "English", "English") \
    X(DarkFrom, "Dunkel ab", "Dark from") \
    X(DarkTo, "Dunkel bis", "Dark until") \
    X(Music, "Musik", "Music") \
    X(Volume, "Lautstärke", "Volume") \
    X(Sounds, "Klänge", "Sounds") \
    X(On, "An", "On") \
    X(Off, "Aus", "Off") \
    X(LibraryDevice, "Bibliotheksgerät", "Library device") \
    X(DeviceUsb, "USB", "USB") \
    X(DeviceSd, "SD", "SD") \
    X(TakesEffectAfterRestart, "nach Neustart", "after restart") \
    X(CoverRegion, "Cover-Region", "Cover region") \
    X(Rescan, "Bibliothek neu scannen", "Rescan library") \
    X(SettingsHelp, "◀▶ Ändern · A Auswählen · B Zurück", "◀▶ Change · A Select · B Back") \
    X(Rescanning, "Bibliothek wird neu durchsucht …", "Rescanning library …") \
    X(Emulators, "Emulatoren", "Emulators") \
    X(EmuCheckUpdates, "Nach Updates suchen", "Check for updates") \
    X(EmuUpdateAll, "Alle aktualisieren", "Update all") \
    X(EmuNotInstalled, "nicht installiert", "not installed") \
    X(EmuCurrent, "aktuell", "up to date") \
    X(EmuUpdateAvailable, "Update verfügbar", "update available") \
    X(EmuUnknownVersion, "Version unbekannt", "unknown version") \
    X(EmuNotInRepo, "nicht im Repository", "not in repository") \
    X(EmuListLoading, "Suche nach Updates …", "Checking for updates …") \
    X(EmuListFailed, "Liste konnte nicht geladen werden (WLAN?)", "Could not load the list (Wi-Fi?)") \
    X(EmuListHint, "Liste noch nicht geladen – erst nach Updates suchen.", "List not loaded yet – check for updates first.") \
    X(EmuStepDownload, "Lade %s …", "Downloading %s …") \
    X(EmuStepVerify, "Prüfe %s …", "Verifying %s …") \
    X(EmuStepExtract, "Entpacke %s …", "Extracting %s …") \
    X(EmuStepMove, "Installiere %s …", "Installing %s …") \
    X(EmuErrNoSpace, "Zu wenig Platz auf der SD-Karte", "Not enough space on the SD card") \
    X(EmuErrDownload, "Download fehlgeschlagen", "Download failed") \
    X(EmuErrBadSize, "Download unvollständig", "Download incomplete") \
    X(EmuErrBadZip, "Paket beschädigt", "Package damaged") \
    X(EmuErrWrite, "Schreiben auf die SD-Karte fehlgeschlagen", "Writing to the SD card failed") \
    X(EmuErrCancelled, "Abgebrochen", "Cancelled") \
    X(EmuHelp, "A Installieren/Aktualisieren · B Zurück", "A Install/Update · B Back") \
    X(EmuBusyHelp, "B Abbrechen", "B Cancel")
// clang-format on

namespace nl::ui {

enum class Str : u16 {
#define NL_STR_ENUM(id, de, en) id,
    NL_STRINGS(NL_STR_ENUM)
#undef NL_STR_ENUM
        Count
};

enum class Lang : u8 { De, En };

const char* tr(Str s);  // Text in der aktiven Sprache, nie nullptr
void setLanguage(Lang l);
Lang language();
Lang resolveLanguage(Config::Language setting, bool systemGerman);
const char* langKey(Lang l);           // "de" / "en"
const char* weekdayName(int weekday0Mon);  // 0 = Montag
const char* monthName(int month1);         // 1 = Januar

}  // namespace nl::ui
