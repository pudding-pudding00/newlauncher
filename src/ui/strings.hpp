#pragma once
// Sichtbare UI-Texte in Deutsch und Englisch (Spec 4.1). Jede Zeile der X-Makro-Tabelle ist
// X(Id, "deutsch", "english"); Texte mit Formatzeichen haben in beiden Sprachen dieselbe Folge
// (strings_test prüft das). Die aktive Sprache ist globaler UI-Zustand, gesetzt von main.

#include <string>

#include "core/config.hpp"
#include "core/net_error.hpp"
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
    X(LaunchNote, "Hinweis", "Note") \
    X(WarnMemcardNoPorts, "Echte Speicherkarte gewählt, aber diese Konsole hat keine GameCube-Ports: Spielstände gehen verloren.", "Real memory card selected, but this console has no GameCube ports: saves will be lost.") \
    X(WarnNativeSiNoPorts, "Native Controller gewählt, aber diese Konsole hat keine GameCube-Ports.", "Native controllers selected, but this console has no GameCube ports.") \
    X(WarnLaunchAgain, "A erneut drücken, um trotzdem zu starten.", "Press A again to start anyway.") \
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
    X(DeviceLine, "Gerät: %s", "Device: %s") \
    X(AlsoOn, "auch auf: %s", "also on: %s") \
    X(NinFormat,"Nintendont startet dieses Format nicht: %s", "Nintendont cannot start this format: %s") \
    X(NinMissing,"Nintendont fehlt.\nNicht gefunden: %s\nInstallieren: Einstellungen → Emulatoren", \
      "Nintendont missing.\nNot found: %s\nInstall: Settings → Emulators") \
    X(NinUnknown, "Nintendont nicht erkannt.\n%s enthält keine Versionsangabe.", \
      "Nintendont not recognized.\n%s contains no version string.") \
    X(NinTooOld, "Nintendont zu alt.\nGefunden %s, nötig 6.493 oder neuer.\nAktualisieren: Einstellungen → Emulatoren", \
      "Nintendont too old.\nFound %s, need 6.493 or newer.\nUpdate: Settings → Emulators") \
    X(NinPathTooLong, "Pfad zu lang.\nNintendont erlaubt höchstens 254 Zeichen: %s", \
      "Path too long.\nNintendont allows at most 254 characters: %s") \
    X(AhbProt, "Kein Hardwarezugriff.\nBitte den Launcher über den Homebrew Channel starten.", \
      "No hardware access.\nPlease start the launcher from the Homebrew Channel.") \
    X(CiosMissing, "cIOS fehlt.\nFür dieses Spiel wird ein cIOS in Slot %s benötigt (d2x empfohlen).", \
      "cIOS missing.\nThis game needs a cIOS in slot %s (d2x recommended).") \
    X(WiiFsUnsupported, "Dateisystem nicht unterstützt.\nWii-Spiele brauchen FAT32 mit 512-Byte-Sektoren (%s).", \
      "File system not supported.\nWii games need FAT32 with 512-byte sectors (%s).") \
    X(WiiFileBroken, "Spieldatei fehlerhaft.\n%s ist beschädigt oder zu stark fragmentiert.", \
      "Game file damaged.\n%s is damaged or too fragmented.") \
    X(DeviceUnsupported, "Gerät nicht unterstützt.\nDas Spiel liegt auf %s. Nintendont und die Emulatoren lesen nur SD und die erste USB-Partition.", \
      "Device not supported.\nThe game is on %s. Nintendont and the emulators only read SD and the first USB partition.") \
    X(CheatsTooLarge, "Zu viele Cheats.\nDie gewählten Codes belegen %s Bytes, zu viel für die Codeliste. Bitte weniger Codes wählen.", \
      "Too many cheats.\nThe selected codes take %s bytes, too much for the code list. Please select fewer codes.") \
    X(FileNotContiguousReadable, "Datei nicht direkt lesbar.\nDatei ist komprimiert oder lückenhaft – bitte unkomprimiert kopieren (%s).", \
      "File not directly readable.\nThe file is compressed or sparse – please copy it uncompressed (%s).") \
    X(NintendontNeedsFat, "Nintendont braucht FAT32.\nGameCube-Spiele auf eine FAT32-Partition kopieren (%s).", \
      "Nintendont needs FAT32.\nCopy GameCube games to a FAT32 partition (%s).") \
    X(WarnEmulatorFs, "%s liest dieses Dateisystem eventuell nicht.", "%s may not read this file system.") \
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
    X(WiiGames, "Wii-Spiele", "Wii games") \
    X(GcGames, "GameCube-Spiele", "GameCube games") \
    X(DetailsHelpOptions, "A Starten · B Zurück · 1 Favorit · +/R Optionen", "A Start · B Back · 1 Favorite · +/R Options") \
    X(OptionsHelp, "◀▶ Ändern · B Speichern und zurück", "◀▶ Change · B Save and back") \
    X(OptResetGame, "Auf Standard zurücksetzen", "Reset to defaults") \
    X(OptResetGlobal, "Zurücksetzen", "Reset") \
    X(OptDefault, "Standard (%s)", "Default (%s)") \
    X(OptNeedsNin, "ab Nintendont %s", "needs Nintendont %s") \
    X(OptBlocks, "%s Blöcke", "%s blocks") \
    X(OptCios, "cIOS %s", "cIOS %s") \
    X(OptVideo, "Videomodus", "Video mode") \
    X(OptVideoPatch, "Videopatch", "Video patch") \
    X(OptVipatch, "VIDTV-Patch", "VIDTV patch") \
    X(OptCountryPatch, "Länder-Patch", "Country patch") \
    X(OptRegionPatch, "Regions-Patch", "Region patch") \
    X(OptAspect, "Seitenverhältnis", "Aspect ratio") \
    X(OptDeflicker, "Entflimmern", "Deflicker") \
    X(OptWidth, "Bildbreite", "Video width") \
    X(OptFix480p, "480p-Fix", "480p fix") \
    X(OptOnline, "Online-Dienst", "Online service") \
    X(OptIos, "IOS", "IOS") \
    X(OptReturnTo, "Rückkehr zu", "Return to") \
    X(OptProgressive, "Progressiv (480p)", "Progressive (480p)") \
    X(OptForceWide, "Breitbild erzwingen", "Force widescreen") \
    X(OptWiiuWide, "Wii-U-Breitbild", "Wii U widescreen") \
    X(OptMemcard, "Speicherkarte", "Memory card") \
    X(OptMemcardBlocks, "Kartengröße", "Card size") \
    X(OptNativeSi, "Native Controller", "Native controllers") \
    X(OptMaxPads, "Max. Controller", "Max controllers") \
    X(OptSkipIpl, "IPL überspringen", "Skip IPL") \
    X(OptCheats, "Cheats", "Cheats") \
    X(OptHook, "Cheat-Hook", "Cheat hook") \
    X(CheatsTitle, "Cheats – %s", "Cheats – %s") \
    X(DetailsCheats, "Cheats …", "Cheats …") \
    X(CheatsNoFile, "Keine Codeliste. +/R lädt Codes.", "No code list. +/R loads codes.") \
    X(CheatsLoading, "Lade Codes …", "Loading codes …") \
    X(CheatsNoCodes, "Keine Codes für dieses Spiel gefunden", "No codes found for this game") \
    X(CheatsNetError, "Codes konnten nicht geladen werden", "Could not load codes") \
    X(CheatsNoSource, "Keine Codequelle (HTTPS nötig)", "No code source (HTTPS required)") \
    X(CheatsReadOnly, "Datenordner nicht beschreibbar", "Data folder is read-only") \
    X(CheatsWiiUnavailable, "Wii-Cheats in dieser Version nicht verfügbar", "Wii cheats not available in this version") \
    X(CheatsOverwrite, "Vorhandene Codeliste ersetzen? A Ja · B Nein", "Replace the existing code list? A Yes · B No") \
    X(CheatsUnselectable, "nicht wählbar", "not selectable") \
    X(CheatsHelp, "A An/Aus · ◀▶ Seite · +/R Codes laden · B Speichern", "A On/Off · ◀▶ Page · +/R Load codes · B Save") \
    X(ValGame, "Spiel", "Game") \
    X(ValSystem, "System", "System") \
    X(ValNoChange, "Keine Änderung", "No change") \
    X(ValNormal, "Normal", "Normal") \
    X(ValMore, "Mehr", "More") \
    X(ValAll, "Alle", "All") \
    X(ValOffExt, "Aus (erweitert)", "Off (extended)") \
    X(ValLow, "Niedrig", "Low") \
    X(ValMedium, "Mittel", "Medium") \
    X(ValHigh, "Hoch", "High") \
    X(ValNoSsl, "Ohne SSL", "No SSL") \
    X(ValNone, "Keine", "None") \
    X(ValEmulated, "Emuliert", "Emulated") \
    X(ValEmulatedMulti, "Emuliert (mehrere)", "Emulated (multi)") \
    X(ValReal, "Echt", "Real") \
    X(ValLangJa, "Japanisch", "Japanese") \
    X(ValLangFr, "Französisch", "French") \
    X(ValLangEs, "Spanisch", "Spanish") \
    X(ValLangIt, "Italienisch", "Italian") \
    X(ValLangNl, "Niederländisch", "Dutch") \
    X(ValLangZhS, "Chinesisch (vereinfacht)", "Chinese (simplified)") \
    X(ValLangZhT, "Chinesisch (traditionell)", "Chinese (traditional)") \
    X(ValLangKo, "Koreanisch", "Korean") \
    X(ValLangEn, "Englisch", "English") \
    X(ValLangDe, "Deutsch", "German") \
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
    X(EmuBusyHelp, "B Abbrechen", "B Cancel") \
    X(About, "Über", "About") \
    X(AboutHttps, "HTTPS: %s", "HTTPS: %s") \
    X(AboutGpl, "Freie Software unter der GNU GPL v3 oder später, ohne jede Gewährleistung.", \
      "Free software under the GNU GPL v3 or later, without any warranty.") \
    X(AboutSource, "Quelltext: Git-Tag des Releases. Lizenztexte: apps/newlauncher/THIRD_PARTY.txt", \
      "Source: the release's Git tag. License texts: apps/newlauncher/THIRD_PARTY.txt") \
    X(AboutComponents, "Enthaltene Komponenten", "Included components") \
    X(AboutHelp, "▲▼ Blättern · B Zurück", "▲▼ Scroll · B Back") \
    X(SysInfo, "Systeminfo", "System info") \
    X(SysInfoHelp, "▲▼ Blättern · A Als Datei speichern · B Zurück", "▲▼ Scroll · A Save as file · B Back") \
    X(SysInfoSaved, "Gespeichert: %s", "Saved: %s") \
    X(SysInfoSaveFailed, "Speichern fehlgeschlagen", "Saving failed") \
    X(Setup, "Einrichtung", "Setup") \
    X(SetupStepOf, "Einrichtung %d/%d", "Setup %d/%d") \
    X(SetupLanguage, "Sprache", "Language") \
    X(SetupLanguageText, "In welcher Sprache soll der Launcher erscheinen?", "Which language should the launcher use?") \
    X(SetupSources, "Bibliotheksquellen", "Library sources") \
    X(SetupSourcesText, "Auf diesen Geräten wird nach Spielen gesucht. Gilt ab dem nächsten Start.", \
      "These devices are searched for games. Takes effect on the next start.") \
    X(SetupCios, "cIOS für Wii-Spiele", "cIOS for Wii games") \
    X(SetupCiosText, "Automatik wählt das passende d2x-cIOS.", "Automatic picks the matching d2x cIOS.") \
    X(SetupEmulators, "Emulatoren", "Emulators") \
    X(SetupEmulatorsText, "Gefundene Emulatoren. Fehlende lassen sich über das Netz installieren.", \
      "Emulators found. Missing ones can be installed over the network.") \
    X(SetupNetwork, "Netzwerk", "Network") \
    X(SetupNetworkText, "Verbindungstest zur Cover-Quelle.", "Connection test to the cover source.") \
    X(SetupNetRunning, "Prüfe Verbindung …", "Checking connection …") \
    X(SetupNetOk, "Cover-Quelle erreichbar", "Cover source reachable") \
    X(SetupNetOkTime, "Cover-Quelle erreichbar, Uhrzeit gestellt", "Cover source reachable, clock set") \
    X(SetupNetFail, "Keine Verbindung (später in den Einstellungen erneut möglich)", \
      "No connection (try again later in Settings)") \
    X(SetupDone, "Fertig", "Done") \
    X(SetupDoneText, "A speichert config.ini. Eine vorhandene Datei bleibt als .bak erhalten.", \
      "A saves config.ini. An existing file is kept as .bak.") \
    X(SetupSuggested, "Vorschlag", "Suggested") \
    X(SetupHelp, "▲▼ Wählen · A Übernehmen · B Überspringen · − Zurück · Home Abbrechen", \
      "▲▼ Choose · A Accept · B Skip · − Back · Home Cancel") \
    X(SetupSaved, "Einrichtung gespeichert", "Setup saved") \
    X(SetupSaveFailed, "Einrichtung nicht gespeichert", "Setup not saved") \
    X(NetErrNone, "Kein Fehler", "No error") \
    X(NetErrDns, "Server nicht gefunden (DNS)", "Server not found (DNS)") \
    X(NetErrConnect, "Verbindung zum Server fehlgeschlagen", "Could not connect to the server") \
    X(NetErrTimeout, "Zeitüberschreitung", "Timed out") \
    X(NetErrCert, "Zertifikat des Servers ungültig", "Server certificate invalid") \
    X(NetErrClock, "Uhrzeit der Wii falsch – Zertifikat nicht prüfbar", "Wii clock is wrong – cannot check the certificate") \
    X(NetErrHandshake, "Sichere Verbindung fehlgeschlagen", "Secure connection failed") \
    X(NetErrNoMem, "Zu wenig Speicher für die Verbindung", "Not enough memory for the connection") \
    X(NetErrTlsMissing, "HTTPS ist in diesem Build nicht enthalten", "HTTPS is not included in this build") \
    X(NetErrHttp, "Server meldet einen Fehler", "Server returned an error") \
    X(NetErrOther, "Netzwerkfehler", "Network error")
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
Str netErrorText(NetError e);              // Klartext je Netzfehler (M11)
// Text des Cover-Dialogs; bei failed > 0 und bekannter Ursache folgt der Klartext dazu in einer zweiten Zeile.
std::string coversSummaryText(u32 ok, u32 missing, u32 failed, NetError lastError);

}  // namespace nl::ui
