# Hardware-Abnahme M13 (System-Erkennung und Einrichtung)

Abnahme auf einer echten Wii, gegen die Release-DOL (`python tools/build_wii.py`, 0 Warnungen). Dolphin hat
bereits gezeigt:
- die Systeminfo-Szene und `sysinfo.txt` (`--expect-sysinfo`);
- den Assistenten beim Erststart ohne `config.ini`, mit Speichern und zweitem Start ohne Assistent
  (`--fresh-data`).

Dolphin emuliert aber weder echte IOS-Slots noch AHBPROT, vWii oder ein USB-Gerät. Diese Teile zeigt erst die
Hardware. Jeder Punkt bekommt PASS, FAIL oder n/a. Bei FAIL kommt eine kurze Notiz in die Zeile und ein Eintrag
unter [Befunde](#befunde). Das Log liegt in `newlauncher/log.txt` des Datenordners.

## Vorbereitung

1. Bauen: `python tools/build_wii.py`.
2. **Vor W2:** `newlauncher/config.ini` (und ggf. `emulators.ini`) der Karte sichern, z. B. auf den PC kopieren.
3. Für W4 ein FAT32-USB-Laufwerk mit `apps/fceugx/boot.dol`; FCEUGX dafür vorübergehend von der SD nehmen.
4. Für W5 RetroArch mit dem Kern `fceumm` (`apps/retroarch-wii/fceumm_libretro_wii.dol`).
5. Keine ROMs, ISOs oder WADs ins Repo legen. Testdaten liegen nur auf den Datenträgern.

## Checkliste

| # | Schritt | Erwartet | Ergebnis |
|---|---|---|---|
| W1 | Einstellungen → Systeminfo; dann „Speichern“ | System-Menü-Version und Region wie unter Wii-Einstellungen; d2x in 249 mit Basis-IOS und Version; AHBPROT „ja“ unter HBC; `newlauncher/sysinfo.txt` geschrieben (`NL_SYSINFO_SAVED`), ohne MAC- oder IP-Adresse | |
| W2 | `config.ini` von der SD löschen, Launcher starten; jeden Vorschlag mit A annehmen, Netztest abwarten, „Speichern“ | Log `NL_CONFIG missing`, `NL_WIZARD start sd=… usb=…` mit plausiblen Zahlen; Vorschläge passen (Sprache, Geräte mit Spielen, d2x-Slot); Netztest grün (`NL_WIZARD net ok`); danach `config.ini` vorhanden (`NL_WIZARD done`); ein Wii-, ein GC- und ein NES-Spiel starten; beim nächsten Start kein Assistent | |
| W3 | `[wii] ios = 250` (wenn installiert) bzw. ein Slot, der fehlt (z. B. `ios = 248`) | Mit vorhandenem Slot startet das Wii-Spiel darüber; fehlender Slot → Dialog mit der Slotnummer, kein Absturz | |
| W4 | FCEUGX nur auf USB (`usb:/apps/fceugx`), NES-Spiel starten | Systeminfo/Emulatorliste nennt `usb:/apps/fceugx/boot.dol`; Spiel startet | |
| W5 | Startmethode eines NES-Spiels auf RetroArch `fceumm` stellen, starten | RetroArch lädt das Spiel direkt (kein Kernmenü) | |
| W6 | (optional, Wii U) Launcher im vWii-Modus starten | Systeminfo zeigt „vWii“ | |

### Hinweise zu den Punkten

- **W1:** Die Systeminfo liest nur. Fehlt ein Wert (z. B. Region ohne `SYSCONF`-Zugriff), steht dort „?“; das
  ist ein Befund, kein Absturzgrund.
- **W2:** Quellen- und Emulatorwahl gelten ab dem nächsten Start, die Sprache sofort. Home bricht den
  Assistenten ohne Schreiben ab; dann erscheint er beim nächsten Start erneut. Eine vorhandene `config.ini`
  bzw. `emulators.ini` wird vor dem Überschreiben als `.bak` gesichert. Der Netztest gleicht im TLS-Build
  vorher die Uhr per SNTP ab („Verbindung ok, Uhrzeit gestellt“).
- **W2 erneut:** Über Einstellungen → Einrichtung lässt sich der Assistent jederzeit wieder öffnen.
- **W3:** Die Slotnummer im Dialog muss der aus `config.ini` entsprechen.
- **W5:** RetroArch bekommt Ordner und Datei getrennt (`{dir}` und `{file}`), siehe
  `docs/superpowers/spikes/m13-retroarch.md`. ZIP-ROMs gehen absichtlich nicht an RetroArch.
- **Nach der Abnahme:** die gesicherte `config.ini` zurückspielen, falls der Assistent andere Werte geschrieben
  hat.

## Befunde

| # | Punkt | Beschreibung | Folge |
|---|---|---|---|
