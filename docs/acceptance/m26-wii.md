# Hardware-Abnahme M26: Lokale Spielstände (nur Wii / vWii)

Status: **ungeprüft** (echte Wii mit HBC, newlauncher, USB-Platte mit Wii-Spielen, Nintendont, Snes9x GX, VBA GX,
EmuNAND auf SD oder USB).
Spec: `docs/superpowers/specs/2026-11-26-cloud-spielstaende-design.md` (Ziele 1–4), Plan
`docs/superpowers/plans/2026-11-26-m26-spielstaende.md`.

**Vor dem ersten Schreiben in den echten NAND (N3 und folgende):** Den Nutzer fragen und ein BootMii-NAND-Backup
empfehlen. Zuerst nur in den Save-Ordner eines Spiels schreiben, das der Nutzer auswählt. Vor jedem Kopieren auf die
SD-Karte fragen.

## Prüfpunkte

| Punkt | Prüfung | Erwartetes Ergebnis | Ergebnis |
|---|---|---|---|
| S1 | newlauncher starten, Details eines Wii-Spiels mit Spielstand, Zeile „Spielstand“ | Szene zeigt Einheit `wii-<ID4>`, Ort `nand:/title/00010000/<hex>/data`, Größe und Dateizahl; Log `NL_IOSPATCH applied` oder `already` (Spike S1) | ungeprüft |
| S2 | Wie S1, aber ohne AHBPROT (anderer Start als über HBC mit `<ahb_access/>`) | Text „NAND gesperrt“, keine Knöpfe; Log `NL_IOSPATCH noahbprot` | ungeprüft |
| N1 | „Sichern“ (nur lesend am NAND) | Meldung „Fertig“, neue Zeile mit Datum/Uhrzeit; ZIP unter `sd:/newlauncher/saves/wii-<ID4>/` lässt sich am PC öffnen, erster Eintrag `newlauncher-save.ini` | ungeprüft |
| N2 | Spiel starten, weiterspielen, speichern, zurück in newlauncher | Größe/Datum unverändert bis zur Szene; neue Sicherung N2 möglich | ungeprüft |
| N3 | **Nach Rückfrage und BootMii-Backup:** ältere Sicherung wählen, „Zurückspielen“, Rückfrage mit A bestätigen | Vorher legt newlauncher selbst eine Sicherung an; Log `NL_SAVE_RESTORE ok`; kein `restore.ini` mehr unter `sd:/newlauncher/saves/` | ungeprüft |
| N4 | Spiel nach N3 starten | Spiel lädt den zurückgespielten Stand | ungeprüft |
| N5 | Im Spiel nach N4 **neu speichern**, Spiel neu starten | Speichern klappt ohne Fehlermeldung (Eigentümer und Rechte der Dateien erhalten), neuer Stand wird geladen | ungeprüft |
| N6 | Rename-Verhalten (Spike S1): Log nach N3 | Kein `Rename (-105)`-Rückfall nötig, oder Rückfall „löschen + umbenennen“ greift; Ergebnis im Spike-Dokument notieren | ungeprüft |
| N7 | Strom während N3 ziehen (nur mit BootMii-Backup und Einverständnis) | Nächster Start: `NL_SAVE_RECOVER rollback`, Spiel lädt den Stand von vor N3 | ungeprüft |
| R1 | Spiel über den Rückkehr-Stub verlassen, sofort Spielstand-Szene öffnen (Spike S2) | Entweder Knöpfe wie in S1, oder „NAND gesperrt“ – Ergebnis notieren | ungeprüft |
| E1 | EmuNAND mit `[wii] save = emu`, Spiel mit Spielstand im EmuNAND | Ort `<gerät>:/<emunand>/title/…/data`; Sichern und Zurückspielen wie N1/N3, Spiel lädt im EmuNAND den Stand | ungeprüft |
| G1 | GameCube-Spiel über Nintendont (Speicherkarte je Spiel) | Einheit `gc-<ID4>`, Ort `<gerät>:/saves/<ID4>.raw`; Sichern/Zurückspielen, Nintendont lädt den Stand | ungeprüft |
| G2 | Nintendont mit Multi-Speicherkarte | Einheit `gc-ninmem` (bzw. `gc-ninmemj`), Ort `<gerät>:/saves/ninmem.raw`; Sichern/Zurückspielen der ganzen Karte | ungeprüft |
| E2 | SNES-Spiel über Snes9x GX | Einheit `snes-<name>-<crc>`, Ort aus dem Profil; Sichern/Zurückspielen, Snes9x GX lädt den Stand | ungeprüft |
| E3 | GBA-Spiel über VBA GX | wie E2 mit `gba-…` | ungeprüft |
| E4 | N64/RetroArch mit eingetragenem `save_dir`/`save_ext` in `emulators.ini` | Datei wird gefunden; Standardpfade für Spike S4 notieren | ungeprüft |
| D1 | Sicherung wählen, „Löschen“ | Zeile verschwindet, Datei ist weg; Log `NL_SAVE_DELETE ok` | ungeprüft |
| D2 | Mehr als 5 Sicherungen anlegen | Älteste fallen weg, höchstens 5 bleiben | ungeprüft |
| L1 | Sprache Englisch | Alle Texte der Szene und der Rückfrage englisch | ungeprüft |

## Dolphin (2026-11-26, grün)

`python tools/dolphin_smoke.py --save-fixture F --save-step S` für jede Kombination aus F ∈ {gb, nand, emunand} und
S ∈ {backup, restore, recover} → `SMOKE OK`:

- **backup:** `NL_SAVE_BACKUP ok <einheit> <pfad>`; das ZIP wird aus `Load/WiiSD.raw` gelesen und mit `zipfile`
  geprüft (Meta zuerst, Einheit, neu gerechneter Inhalts-Hash, Bytes).
- **restore:** veränderte Datei + `save_restore=<key>` → `NL_SAVE_RESTORE ok`, Datei hat wieder die gesicherten Bytes
  (nand: im Dolphin-NAND des Dev-Users, sonst im SD-Abbild).
- **recover:** Journal mit `phase = writing` und hinzugekommener Datei → `NL_SAVE_RECOVER rollback`, gesicherte Bytes
  zurück, hinzugekommene Datei entfernt.
- nand: `NL_IOSPATCH emulated` (Dolphin hat kein AHBPROT, schreibt aber in `/title/…/data`, Spike S3).
