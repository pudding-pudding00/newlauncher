# Hardware-Abnahme M19: Weitere Startwege (nur Wii / vWii)

Status: **ungeprüft** (erfordert eine echte Wii bzw. vWii mit optischem Laufwerk und optionalem EmuNAND).

## Vorbereitung

- Original-Wii-Disc (und falls Punkt W2 geprüft wird, eine zweite Original-Wii-Disc zum Wechseln).
- Optional für W4: Original-GameCube-Disc und Nintendont auf SD oder USB (nur Wii mit GC-Ports).
- Für W5/W6: d2x-cIOS (z. B. Slot 249), vorbereiteter EmuNAND auf SD-Karte oder primärer USB-FAT32-Partition mit mindestens einem installierten Kanal (`title/00010001/...`) und in `newlauncher/config.ini` eingetragenem `[emunand]\npath = usb:/...` (bzw. `sd:/...`).
- Optional für W7: vWii (Wii U) mit GameCube-Spiel auf USB/SD und angeschlossener Wiimote + Classic Controller oder HID-Controller.

## Prüfpunkte

| Punkt | Prüfung | Erwartetes Ergebnis | Ergebnis |
|---|---|---|---|
| W1 | Original-Wii-Disc einlegen (beim Start oder über HOME → „Disc lesen“), Spiel mit Optionen aus M14 (z. B. `video`/`aspect`) starten | Eintrag mit Plakette „Disc“ erscheint an erster Stelle der Wii-Spiele; Spiel startet direkt vom Laufwerk mit den gesetzten Optionen | ungeprüft |
| W2 | Eingelegte Wii-Disc anzeigen lassen, Disc im Laufwerk wechseln, dann alten Eintrag starten | Fehlermeldung „Andere Disc eingelegt“ mit neuer ID6; Bibliothek aktualisiert sich danach auf die neue Disc | ungeprüft |
| W3 | Leeres Laufwerk bzw. Daten-/Video-DVD einlegen und Start/Aktualisierung prüfen | Kein falscher Disc-Eintrag; Oberfläche ruckelt oder blockiert nicht (5-s-Zeitlimit im Hintergrund) | ungeprüft |
| W4 | Original-GC-Disc einlegen (auf Wii mit GC-Ports) und über Nintendont starten | Eintrag unter GameCube erscheint mit Plakette „Disc“; Nintendont startet direkt die Disc (`GamePath = di`, ohne `usb`-Flag) mit den GC-Optionen | ungeprüft |
| W5 | Mit eingerichtetem EmuNAND (SD oder primäre USB-FAT32-Partition) starten, Kategorie „Kanäle“ öffnen und einen Kanal starten | Kanäle erscheinen mit lokalisiertem Namen (IMET); gewählter Kanal zeigt Banner und startet über d2x-Voll-EmuNAND | ungeprüft |
| W6 | Bei einem Wii-Spiel „Spielstände = Emuliert“ (`save = emu`) setzen und starten, etwas speichern, danach auf „Echt“ oder am echten NAND prüfen | Spielstand landet ausschließlich im EmuNAND; echter Wii-NAND bleibt unverändert | ungeprüft |
| W7 | Optional auf vWii: GC-Spieloptionen öffnen und starten | `wiiu_wide` ist standardmäßig aktiv; `memcard = real` / `native_si` zeigen „Auf vWii nicht verfügbar“; Hilfe nennt Wiimote + Classic / HID; keine GC-Port-Warnung beim Start | ungeprüft |
