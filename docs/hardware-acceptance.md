# Hardware-Abnahme M7

Abnahme von newlauncher auf einer echten Wii, gegen die **Release-DOL** `build/wii/newlauncher.dol`.
Dolphin deckt Grafik, Eingabe und die Startwege gut ab. Zeitverhalten, USB-Anlauf, cIOS, echte
Emulatoren und Kanäle zeigt nur die Hardware (Spec §10).

Jeder Punkt hat einen Schritt und ein erwartetes Ergebnis. In „Ergebnis“ kommt PASS oder FAIL, bei
FAIL mit einer kurzen Notiz und einer Zeile unter [Befunde](#befunde).

## Vorbereitung

1. Bauen: `python tools/build_wii.py`
2. Release-Karte (die normale SD-Karte mit Homebrew Channel):
   ```bash
   python tools/wii_deploy.py E: --perf on
   ```
   Kopiert `apps/newlauncher/boot.dol` und `meta.xml`. Vorhandene Daten unter `newlauncher/`
   (`config.ini`, `emulators.ini`, Favoriten, Verlauf, `cache/`) bleiben unverändert. Einzige Ausnahme
   ist `--perf on`: Es setzt in `config.ini` die Zeile `perf = 1` unter `[debug]`.
3. Selbsttest-Karte (eine **zweite, leere** SD-Karte, nur für X1 und P3):
   ```bash
   python tools/wii_deploy.py F: --selftest
   ```
4. Alternativ ohne Kartenwechsel per WLAN (HBC offen, IP aus der HBC-Ecke unten rechts):
   ```bash
   python tools/wii_deploy.py --wiiload 192.168.0.42
   ```
   Für P1/P2 trotzdem einmal `--perf on` auf der Karte setzen: wiiload überträgt nur die DOL.
5. Nach der Abnahme: `python tools/wii_deploy.py E: --perf off`

## V – Voraussetzungen

Nur notieren. Diese Daten braucht M8 (USB-Loader-Start, cIOS) und M9 (Nintendont).

| ID | Schritt | Erwartet | Ergebnis |
|---|---|---|---|
| V1 | Wii-Modell (RVL-001/-101) und Systemmenü-Version (Wii-Optionen, oben rechts) | notiert | |
| V2 | HBC-Version (HBC, HOME-Taste) | notiert | |
| V3 | cIOS 249: d2x-Version und Basis-IOS laut d2x-Installer bzw. Anzeige beim Start eines USB-Loaders | notiert | |
| V4 | Nintendont-Version (Titelbildschirm) | notiert | |
| V5 | USB-Platte: Modell, Dateisystem, Clustergröße, Sektorgröße. Windows: `fsutil fsinfo ntfsinfo E:` bzw. `fsutil fsinfo sectorinfo E:`; bei FAT32 laut Laufwerkseigenschaften | notiert | |
| V6 | SD-Karte: Größe, FAT32 | notiert | |
| V7 | Videoeinstellung im Wii-Menü: 50/60 Hz, 480p, 4:3/16:9 | notiert | |

## S – Start und Laufwerke

| ID | Schritt | Erwartet | Ergebnis |
|---|---|---|---|
| S1 | newlauncher im HBC starten | Splash erscheint sofort, kein schwarzes Bild über ~0,5 s | |
| S2 | Platte angeschlossen und wach, `library = usb` | Bibliothek erscheint ohne Dialog | |
| S3 | Platte frisch einstecken bzw. kalt starten | „Warte auf USB …“ mit Countdown, danach Bibliothek | |
| S4 | Platte abziehen, starten | Dialog nach `usb_timeout` Sekunden; „SD verwenden“ öffnet die SD-Bibliothek | |
| S5 | HOME → Einstellungen → Bibliotheksgerät: SD, Neustart | Bibliothek von der SD, Hinweis auf Neustart erschien vorher | |
| S6 | `newlauncher/cache/index.bin` auf dem Bibliotheksgerät löschen, starten | Scan-Fortschritt, danach bedienbar | |
| S7 | Mit `--perf on`: Platte schlafen lassen bzw. kalt anstecken, `usb_timeout` kleiner als die Anlaufzeit, im Dialog einmal „Erneut versuchen“, Wartezeit mit der Uhr stoppen; danach sauber beenden und `check_perf.py` | `usb_wait_ms` in `NL_PERF summary` ≥ gestoppte Zeit (inkl. Dialog); „Start“ PASS | |

## D – Darstellung

| ID | Schritt | Erwartet | Ergebnis |
|---|---|---|---|
| D1 | Wii-Menü auf 4:3 / 480i | Layout vollständig, nichts abgeschnitten, Kreise rund | |
| D2 | 16:9 / 480p (Komponentenkabel) | Layout breit, Kreise rund, kein Flimmern | |
| D3 | PAL50 (falls einstellbar, EURGB60 aus) | Bild stabil, Animationen gleich schnell | |
| D4 | Theme hell, dunkel, automatisch | Wechsel mit Überblendung, automatisch nach Uhrzeit | |
| D5 | Titel mit Umlauten ansehen | Schrift scharf, Umlaute korrekt | |

## E – Eingabe

| ID | Schritt | Erwartet | Ergebnis |
|---|---|---|---|
| E1 | Wiimote auf den Bildschirm richten | Zeiger folgt ohne Zittern, Kacheln reagieren | |
| E2 | Steuerkreuz der Wiimote | Fokus wandert, Wiederholung beim Halten | |
| E3 | Classic Controller | Navigation und A/B wie Wiimote | |
| E4 | GameCube-Controller | Navigation und A/B wie Wiimote | |
| E5 | vier Wiimotes verbinden, HOME-Menü | Akkustand P1–P4 angezeigt | |
| E6 | Debug-Overlay ein/aus: Wiimote 1 + 2, GameCube Z + Start | Overlay mit Frame-Statistik erscheint und verschwindet | |

## B – Bibliothek

| ID | Schritt | Erwartet | Ergebnis |
|---|---|---|---|
| B1 | Spiel favorisieren, ein Spiel starten, zurück | Favorit und „Zuletzt gespielt“ bleiben nach Neustart | |
| B2 | A–Z-Sprungleiste | Sprung auf den Buchstaben | |
| B3 | Suche mit Bildschirmtastatur | Treffer während der Eingabe | |
| B4 | Detailansicht öffnen | Cover, Metadaten, Startmethode | |
| B5 | HOME → Einstellungen → „Bibliothek neu scannen“ | Toast, neue Spiele erscheinen, UI bleibt bedienbar | |

## L – Emulatoren

Risiko Spec §10: das `argv`-Schema der Emulatoren. Je ein ROM aus der eigenen Sammlung muss **direkt im
Emulator** starten, ohne dessen Dateiauswahl.

| ID | Schritt | Erwartet | Ergebnis |
|---|---|---|---|
| L1 | NES-ROM starten (FCE Ultra GX) | Spiel läuft | |
| L2 | SNES-ROM starten (Snes9x GX) | Spiel läuft | |
| L3 | GB-ROM starten | Spiel läuft | |
| L4 | GBC-ROM starten | Spiel läuft | |
| L5 | GBA-ROM starten (Visual Boy Advance GX) | Spiel läuft | |
| L6 | N64-ROM mit Not64 starten | Spiel läuft | |
| L7 | N64-ROM mit Wii64 starten | Spiel läuft | |
| L8 | Emulator-DOL umbenennen, ROM starten | Preflight-Dialog nennt den fehlenden Emulator, Launcher bleibt bedienbar | |
| E1 | Emulatoren auf echter Wii installieren/aktualisieren (WLAN): Einstellungen → Emulatoren → „Nach Updates suchen“, einen fehlenden Emulator installieren, danach „Alle aktualisieren“; einmal mitten im Download B drücken | Liste lädt (`NL_EMU_LIST ok`), Installation meldet `NL_EMU_INSTALL … ok`, Zeile zeigt „aktuell“; Abbruch zeigt „Abgebrochen“, alte Version startet weiter, `sd:/newlauncher/tmp/` ist leer | |

## N – N64 Virtual Console

| ID | Schritt | Erwartet | Ergebnis |
|---|---|---|---|
| N1 | HOME → Einstellungen → „VC-Injects zuordnen“ | Kanalliste mit Bannernamen (Risiko Spec §10) | |
| N2 | Zuordnung speichern, Neustart | Zuordnung bleibt (`n64_vc.ini`) | |
| N3 | Inject starten | Kanal startet das Spiel | |

## G – GameCube

Voraussetzung: Nintendont ab 6.493 unter `sd:/apps/nintendont/boot.dol`, Launcher aus dem HBC-Eintrag gestartet.

| ID | Schritt | Erwartet | Ergebnis |
|---|---|---|---|
| G1 | GC-Spiel von SD starten (`sd:/games/<Titel> [ID6]/game.iso`) | Nintendont startet das Spiel ohne eigenes Menü; Log zeigt `NL_NIN ver=… usb=0` | |
| G2 | GC-Spiel von USB starten (Bibliotheksgerät USB) | Spiel startet; `NL_NIN … usb=1` | |
| G3 | Im Spiel auf der Memory Card speichern, Wii neu starten, Spiel erneut starten und laden | Spielstand ist da (emulierte Karte unter `saves/`) | |
| G4 | Zwei-Disc-Spiel mit `disc2.iso` bis zum Disc-Wechsel spielen | Nintendont wechselt auf Disc 2 – oder „kein Zwei-Disc-Spiel vorhanden“ | |
| G5 | `sd:/apps/nintendont/boot.dol` umbenennen, GC-Spiel starten | Dialog „Nintendont fehlt“ mit dem Pfad, Launcher bleibt bedienbar | |
| G6 | Launcher per `wiiload` starten, GC-Spiel starten | Log `NL_AHBPROT on`, Dialog „Kein Hardwarezugriff“ | |
| G7 | GC-Controller und Wiimote+Classic Controller im Spiel benutzen | beide steuern das Spiel | |

## C – Cover

| ID | Schritt | Erwartet | Ergebnis |
|---|---|---|---|
| C1 | HOME → Einstellungen → „Fehlende Cover laden“ (WLAN) | Fortschritt, danach Ergebnisdialog; UI bleibt bedienbar | |
| C2 | PNG in `covers_src/` legen, Spiel anzeigen | Cover erscheint nach der Umwandlung, `.tex` liegt danach im Cache | |
| C3 | Schnell durch die ganze Bibliothek scrollen | kein Ruckeln, Cover blenden ein | |

## A – Audio und Einstellungen

| ID | Schritt | Erwartet | Ergebnis |
|---|---|---|---|
| A1 | `.ogg` in `sd:/newlauncher/music/`, Musik an | Musik läuft nach dem Start | |
| A2 | Lautstärke ändern | sofort hörbar | |
| A3 | UI-Sounds aus und wieder an | Klicks verstummen bzw. kommen zurück | |
| A4 | Sprache DE/EN | Texte wechseln sofort | |
| A5 | Einstellungen ändern, Neustart | Werte bleiben | |
| A6 | Spiel starten, während Musik läuft | Musik endet vor dem Spielstart, kein Knacken im Spiel | |

## H – HOME-Menü

| ID | Schritt | Erwartet | Ergebnis |
|---|---|---|---|
| H1 | HOME → zurück zum HBC | HBC erscheint | |
| H2 | HOME → Wii-Menü | Wii-Menü erscheint | |
| H3 | HOME → Ausschalten | Wii geht aus (Standby bzw. aus, je nach WC24-Einstellung) | |

## X – Absturz

| ID | Schritt | Erwartet | Ergebnis |
|---|---|---|---|
| X1 | Selbsttest-Karte: „newlauncher Selbsttest“ im HBC starten, ohne Eingaben warten | Skript läuft sichtbar ab, danach Absturzbildschirm mit Ausnahme 7 (Foto); RESET führt zum HBC | |

## P – Performance

Budgets aus Spec §7. Die Messung läuft nur mit `[debug] perf = 1`.

| ID | Schritt | Erwartet | Ergebnis |
|---|---|---|---|
| P1 | Release-Karte mit `--perf on`, Cache vorhanden (zweiter Start), Platte wach: starten, 2 min scrollen, „Fehlende Cover laden“, „Bibliothek neu scannen“, ein Spiel starten | Spiel startet | |
| P2 | `python tools/check_perf.py E:\newlauncher\log.txt F:\newlauncher\log.txt` | Exit 0, Tabelle unten eingefügt | |
| P3 | Selbsttest-Karte nach X1: `F:\newlauncher\log.txt` | enthält `NL_ALLOC_IDLE 0` und `NL_SELFTEST_OK` | |

`log.txt` ist ein Ringpuffer mit den letzten 16 KB. In langen Sitzungen fehlen `NL_PERF boot` und
frühe Fenster. `check_perf.py` urteilt dann aus der letzten `NL_PERF summary` (sie enthält auch
`busy_slow`, die langsamen Frames während Hintergrundarbeit), markiert fehlende Zeilen als N/A und
weist auf fehlende Fensterzeilen hin. Die eigenen Log-Schreibvorgänge der Messung (`NL_READY`,
`NL_COVERS`, `NL_COVER_DL_DONE`) zählen nicht als langsame Frames.

Ergebnis von `check_perf.py`:

<!-- Tabelle hier einfügen -->

## Befunde

| ID | Beobachtung | Ursache | Fix-Commit | erneut geprüft |
|---|---|---|---|---|
| | | | | |
