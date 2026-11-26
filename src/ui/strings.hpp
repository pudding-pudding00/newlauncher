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
    X(ThemePack, "Theme-Paket", "Theme pack") \
    X(ThemePackDefault, "Standard", "Default") \
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
    X(Filter, "Filter", "Filter") \
    X(FilterAll, "Alle", "All") \
    X(Categories, "Kategorien", "Categories") \
    X(SortOrder, "Sortierung", "Sort order") \
    X(SortTitle, "Titel", "Title") \
    X(SortYear, "Jahr", "Year") \
    X(SortRecent, "Zuletzt gestartet", "Recently played") \
    X(SortMost, "Am häufigsten", "Most played") \
    X(SortedBy, "Sortiert nach", "Sorted by") \
    X(StatsStarts, "%u-mal gestartet", "Started %u times") \
    X(StatsOnce, "Einmal gestartet", "Started once") \
    X(StatsLast, ", zuletzt am %s", ", last on %s") \
    X(Parental, "Kindersicherung", "Parental controls") \
    X(ParentalSetPin, "PIN festlegen", "Set PIN") \
    X(ParentalChangePin, "PIN ändern", "Change PIN") \
    X(ParentalSystem, "Altersfreigabe nach", "Rating system") \
    X(ParentalMaxAge, "Altersgrenze", "Age limit") \
    X(ParentalLockUnrated, "Spiele ohne Freigabe sperren", "Lock unrated games") \
    X(ParentalUseWii, "Wii-Jugendschutz übernehmen", "Use Wii parental controls") \
    X(ParentalRemovePin, "PIN entfernen (Kindersicherung aus)", "Remove PIN (turn off)") \
    X(ParentalOff, "Aus", "Off") \
    X(ParentalAgeYears, "ab %u", "%u+") \
    X(PinEnter, "PIN eingeben", "Enter PIN") \
    X(PinNew, "Neue PIN", "New PIN") \
    X(PinRepeat, "PIN wiederholen", "Repeat PIN") \
    X(PinMismatch, "PINs stimmen nicht überein", "PINs do not match") \
    X(PinWrong, "Falsche PIN", "Wrong PIN") \
    X(PinBlocked, "Zu viele Versuche, bitte 30 s warten", "Too many attempts, wait 30 s") \
    X(PinHelp, "▲▼ Ziffer · ◀▶ Stelle · A Weiter · B Abbrechen", "▲▼ Digit · ◀▶ Position · A Next · B Cancel") \
    X(PinSet, "PIN gespeichert", "PIN saved") \
    X(PinRemoved, "Kindersicherung aus", "Parental controls off") \
    X(GenreTheme, "Thema", "Theme") \
    X(GenreGeneral, "Allgemein", "General") \
    X(GenreAction, "Action", "Action") \
    X(GenreSports, "Sport", "Sports") \
    X(GenreAdventure, "Abenteuer", "Adventure") \
    X(GenreMusic, "Musik", "Music") \
    X(GenreRacing, "Rennspiel", "Racing") \
    X(GenreFighting, "Kampfspiel", "Fighting") \
    X(GenreSimulation, "Simulation", "Simulation") \
    X(GenreRolePlaying, "Rollenspiel", "Role-playing") \
    X(GenreStrategy, "Strategie", "Strategy") \
    X(GenreTraditional, "Traditionell", "Traditional") \
    X(GenreHealth, "Gesundheit", "Health") \
    X(GenreOthers, "Sonstiges", "Others") \
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
    X(ClearCovers, "Cover-Cache löschen", "Clear cover cache") \
    X(ClearCoversHold, "A halten zum Löschen …", "Hold A to delete …") \
    X(ClearingCovers, "Cover werden gelöscht … %u", "Deleting covers … %u") \
    X(CoversCleared, "Cover-Cache gelöscht (%u Dateien)", "Cover cache cleared (%u files)") \
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
    X(Banners, "Banner", "Banners") \
    X(On, "An", "On") \
    X(Off, "Aus", "Off") \
    X(LibraryDevice, "Bibliotheksgerät", "Library device") \
    X(DeviceUsb, "USB", "USB") \
    X(DeviceSd, "SD", "SD") \
    X(TakesEffectAfterRestart, "nach Neustart", "after restart") \
    X(CoverRegion, "Cover-Region", "Cover region") \
    X(CoverAuto, "Cover automatisch laden", "Download covers automatically") \
    X(CoverStorage, "Cover-Speicher", "Cover storage") \
    X(CoverStorageSd, "SD-Karte", "SD card") \
    X(CoverStorageDevice, "Spielgerät", "Game drive") \
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
    X(DetailsCheats, "Cheats …", "Cheats …")     X(DetailsSave, "Spielstand …", "Save data …")     X(SavesTitle, "Spielstand – %s", "Save data – %s")     X(SavesBackup, "Sichern", "Back up")     X(SavesRestore, "Wiederherstellen", "Restore")     X(SavesDelete, "Sicherung löschen", "Delete backup")     X(SavesEmpty, "Noch kein Spielstand", "No save data yet")     X(SavesNandLocked, "NAND gesperrt – Neustart über den Homebrew Channel",       "NAND locked – restart from the Homebrew Channel")     X(SavesUnknown, "Speicherort unbekannt (emulators.ini: save_dir)", "Save location unknown (emulators.ini: save_dir)")     X(SavesConfirm, "Sicherung vom %s zurückspielen? Der jetzige Stand wird vorher gesichert.",       "Restore the backup from %s? The current save is backed up first.")     X(SavesDone, "Fertig", "Done")     X(SavesFailed, "Fehlgeschlagen: %s", "Failed: %s")     X(SavesNoBackup, "Keine Sicherungen", "No backups")     X(SavesSize, "%s · %u Dateien", "%s · %u files") \
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
    X(EmuInstallAll, "Alle installieren", "Install all") \
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
    X(Wifi, "WLAN", "Wi-Fi") \
    X(WifiConnected, "Verbunden", "Connected") \
    X(WifiLimited, "Eingeschränkt: Router antwortet nicht", "Limited: router not responding") \
    X(WifiConnecting, "Verbinde …", "Connecting …") \
    X(WifiDisconnected, "Nicht verbunden", "Not connected") \
    X(WifiLineState, "Zustand: %s", "Status: %s") \
    X(WifiLineIp, "IP-Adresse: %s", "IP address: %s") \
    X(WifiLineMask, "Netzmaske: %s", "Netmask: %s") \
    X(WifiLineGateway, "Router: %s", "Router: %s") \
    X(WifiLineRouterMs, "Router-Antwort: %d ms", "Router reply: %d ms") \
    X(WifiLineRouterNone, "Router-Antwort: keine", "Router reply: none") \
    X(WifiLineMac, "MAC-Adresse: %s", "MAC address: %s") \
    X(WifiLineCode, "Letzter Fehlercode: %d", "Last error code: %d") \
    X(WifiLineFails, "Fehlschläge in Folge: %u", "Failures in a row: %u") \
    X(WifiLineSince, "Verbunden seit: %s", "Connected for: %s") \
    X(WifiLineChecked, "Letzte Prüfung: vor %u s", "Last check: %u s ago") \
    X(WifiLineNotChecked, "Letzte Prüfung: noch keine", "Last check: none yet") \
    X(WifiLineReconnects, "Neuaufbauten: %u", "Reconnects: %u") \
    X(WifiTips, "Tipps:", "Tips:") \
    X(WifiTip1, " · Router: WPA2-AES statt WPA3 oder WPA2/WPA3-Mischbetrieb", \
      " · Router: WPA2-AES instead of WPA3 or WPA2/WPA3 mixed mode") \
    X(WifiTip2, " · Eigenes 2,4 GHz-Netz mit eigenem Namen (kein Band-Steering)", \
      " · Separate 2.4 GHz network with its own name (no band steering)") \
    X(WifiTip3, " · PMF / 802.11w aus oder optional", " · PMF / 802.11w off or optional") \
    X(WifiTip4, " · Fester Kanal 1, 6 oder 11", " · Fixed channel 1, 6 or 11") \
    X(WifiTip5, " · Feste IP in den Wii-Einstellungen", " · Static IP in the Wii settings") \
    X(WifiTip6, " · Wii ganz vom Strom trennen, Standby/WiiConnect24 aus", \
      " · Unplug the Wii fully, turn off standby/WiiConnect24") \
    X(WifiHelp, "▲▼ Blättern · A Neu verbinden · B Zurück", "▲▼ Scroll · A Reconnect · B Back") \
    X(WifiBusyHelp, "Verbinde neu … · B Zurück", "Reconnecting … · B Back") \
    X(WifiToastFailed, "WLAN: keine Verbindung (Code %d), neuer Versuch folgt", \
      "Wi-Fi: no connection (code %d), retrying") \
    X(WifiToastLost, "WLAN-Verbindung verloren, neuer Versuch folgt", "Wi-Fi connection lost, retrying") \
    X(WifiToastRestored, "WLAN verbunden", "Wi-Fi connected") \
    X(WifiToastLimited, "WLAN eingeschränkt: Router antwortet nicht", "Wi-Fi limited: router not responding") \
    X(About, "Über", "About") \
    X(SelfUpdate, "Update installieren", "Install update") \
    X(RestartSelf, "newlauncher neu starten", "Restart newlauncher") \
    X(UpdateTitle, "Update", "Update") \
    X(UpdateChecking, "Suche nach Updates …", "Checking for updates …") \
    X(UpdateProgress, "Update wird geladen … %u %%", "Downloading update … %u %%") \
    X(UpdateCurrent, "newlauncher ist aktuell (Version %s).", "newlauncher is up to date (version %s).") \
    X(UpdateInstalled, "Version %s ist installiert.\nNach OK startet newlauncher neu.", \
      "Version %s is installed.\nnewlauncher restarts after OK.") \
    X(UpdateNoManifest, "Keine Update-Information erreichbar.\nBitte Netzwerk prüfen.", \
      "No update information reachable.\nPlease check the network.") \
    X(UpdateFailed, "Update fehlgeschlagen (%s).\nDie bisherige Version bleibt.", \
      "Update failed (%s).\nThe current version stays.") \
    X(UpdateBusy, "Gerade läuft ein anderer Download.", "Another download is running.") \
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
    X(SetupHelp, "▲▼ Wählen · A Übernehmen · B Überspringen · - Zurück · Home Abbrechen", \
      "▲▼ Choose · A Accept · B Skip · - Back · Home Cancel") \
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
    X(NetErrOther, "Netzwerkfehler", "Network error") \
    X(Disc, "Disc", "Disc") \
    X(ReadDisc, "Disc lesen", "Read disc") \
    X(ReadingDisc, "Disc wird gelesen …", "Reading disc …") \
    X(DiscUnreadable, "Disc nicht lesbar", "Disc not readable") \
    X(NoDisc, "Keine Disc eingelegt", "No disc inserted") \
    X(DiscChanged, "Andere Disc eingelegt.\nIm Laufwerk liegt eine andere Disc (%s). Die Liste wurde aktualisiert.", \
      "Different disc inserted.\nA different disc (%s) is in the drive. The list has been updated.") \
    X(DiscUnreadableErr, "Disc nicht lesbar.\nBitte prüfen, ob die Disc eingelegt und sauber ist.", \
      "Disc not readable.\nPlease check that the disc is inserted and clean.") \
    X(GcDiscUnsupported, \
      "GameCube-Disc nicht unterstützt.\nDieses Laufwerk (vWii / ohne GameCube-Anschlüsse) kann keine GameCube-Discs lesen.", \
      "GameCube disc not supported.\nThis drive (vWii / without GameCube ports) cannot read GameCube discs.") \
    X(Channels, "Kanäle", "Channels") \
    X(OptSave, "Spielstände", "Save data") \
    X(NoEmuNand, \
      "Kein EmuNAND eingerichtet.\nBitte [emunand] path in config.ini angeben und prüfen, ob der Ordner existiert.", \
      "No EmuNAND configured.\nSet [emunand] path in config.ini and check that the folder exists.") \
    X(EmuNandNeedsD2x, "d2x-cIOS benötigt.\nEmuNAND erfordert ein d2x-cIOS (z. B. in Slot 249).", \
      "d2x cIOS required.\nEmuNAND requires a d2x cIOS (e.g. in slot 249).") \
    X(EmuNandNeedsFat, \
      "EmuNAND braucht FAT32.\nDas EmuNAND muss auf der SD-Karte oder einer primären FAT32-Partition liegen (%s).", \
      "EmuNAND needs FAT32.\nThe EmuNAND must be on the SD card or a primary FAT32 partition (%s).") \
    X(OptNotOnVWii, "Auf vWii nicht verfügbar", "Not available on vWii") \
    X(OptionsHelpVWii, "◀▶ Ändern · B Speichern · vWii: Wiimote + Classic / HID-Controller", \
      "◀▶ Change · B Save · vWii: Wiimote + Classic / HID controller")
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
const char* genreLabel(u8 genre);          // Index in kGenres (M17); unbekannt → "Sonstiges"
const char* consoleLabel(Console c);       // lokalisierte Konsolenbezeichnung (Kanäle DE/EN)
// Text des Cover-Dialogs; bei failed > 0 und bekannter Ursache folgt der Klartext dazu in einer zweiten Zeile.
std::string coversSummaryText(u32 ok, u32 missing, u32 failed, NetError lastError);

}  // namespace nl::ui
