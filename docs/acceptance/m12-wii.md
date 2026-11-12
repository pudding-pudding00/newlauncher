# Hardware-Abnahme M12 (Bibliothek für jedes Setup)

Abnahme auf einer echten Wii, gegen die Release-DOL (`python tools/build_wii.py`, 0 Warnungen). Dolphin hat
bereits gezeigt:
- Ordnerregeln (`[paths]`, `[extensions]`);
- ZIP-ROMs im Index (`--sd-layout multi`);
- den Wii-Dry-Run.

Dolphin hat aber nur ein FAT-Gerät (die SD). Diese Teile zeigt erst die Hardware:
- den Datenordner auf USB;
- den Start von USB;
- mehrere Partitionen;
- dasselbe Spiel auf zwei Geräten;
- exFAT.

Jeder Punkt bekommt PASS oder FAIL. Bei FAIL kommt eine kurze Notiz in die Zeile und ein Eintrag unter
[Befunde](#befunde). Das Log liegt in `newlauncher/log.txt` des Datenordners.

## Vorbereitung

1. Bauen: `python tools/build_wii.py`.
2. **Vor W1:** die v1-Karte unverändert lassen und nur `boot.dol` ersetzen.
3. Ein USB-Laufwerk mit FAT32. Für W4 eine zweite FAT32-Partition, falls die Platte das zulässt. Für W7 ein
   exFAT-Stick.
4. Keine ROMs, ISOs oder WADs ins Repo legen. Testdaten liegen nur auf den Datenträgern.

## Checkliste

| # | Schritt | Erwartet | Ergebnis |
|---|---|---|---|
| W1 | v1-`config.ini` unverändert lassen, Launcher starten | Bibliothek, Favoriten und Cover wie vorher; Log `NL_MOUNT` nennt dieselben Geräte wie v1; kein `NL_CFG_BAD` | |
| W2 | SD entnehmen, `newlauncher/` liegt auf USB, Launcher über USB-HBC starten | Log `NL_DATA usb:/newlauncher`; Launcher voll bedienbar (Liste, Einstellungen, Favorit setzen) | |
| W3 | App von `usb:/apps/newlauncher` starten | Log `NL_DATA usb:/newlauncher`; Favoriten und Verlauf landen auf USB | |
| W4 | Zweite FAT-Partition mit einem Wii-Spiel unter `wbfs/` | Log `NL_DEV usb2 fat mounted`; `NL_MOUNT` enthält `usb2:`; Spiel in der Liste, Detailansicht „Gerät: USB 2“; Spiel startet von `usb2` (`NL_FRAG dev=usb2`) | |
| W5 | Dasselbe Spiel (gleicher Schlüssel) auf SD und USB | Spiel genau einmal in der Liste; Detailansicht „Gerät: USB 1 (auch auf: SD)“ (EN „Device: USB 1 (also on: SD)“) | |
| W6 | Eine NES-ROM als `.zip` und eine SNES-ROM als `.zip` in `roms/` | Beide in der Liste und starten in FCEUGX bzw. Snes9xGX | |
| W7 | exFAT-Stick einstecken und starten | Log `NL_DEV - exfat skipped lba=… (exFAT wird nicht unterstützt – bitte FAT32)`; kein Absturz, übrige Geräte normal | |

### Hinweise zu den Punkten

- **W1:** Der Alt-Schlüssel `[general] library = usb|sd` wird zu einer festen Quelle. Mit `library = usb` und
  fehlendem USB erscheint wie in v1 der Dialog „Erneut versuchen / SD verwenden“.
- **W2/W3:** Ist ein Gerät nicht beschreibbar, landen dessen `index.bin` und Cover unter
  `<data>/cache/<gerät>/`.
- **W4:** Lässt die Platte keine zweite Partition zu, bekommt der Punkt „n/a“ mit Begründung.
- **W5:** Welches Gerät gewinnt, entscheidet die Reihenfolge in `[library] sources`. Bei `auto` gilt die
  Gerätereihenfolge.
- **W6:** `.zip` ist für NES, SNES, GB, GBC und GBA Standardendung. Eine eigene `[extensions]`-Zeile ersetzt
  die Liste; dann muss `.zip` dort mit drinstehen.
- **W7:** In M12 erscheint der Hinweis nur im Log. Eine Geräteliste in den Einstellungen gibt es noch nicht.
- **Wartezeit:** Bei `sources = auto` wartet der Start ohne USB-Laufwerk bis zu `usb_timeout` Sekunden.
  Bitte die echte Dauer notieren. Fällt sie störend auf, kommt das unter Befunde.

## Befunde

| # | Punkt | Beschreibung | Folge |
|---|---|---|---|
