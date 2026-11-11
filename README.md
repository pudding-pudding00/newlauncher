# newlauncher

Hochperformanter Wii-Launcher im Wii-Stil für Homebrew/cIOS-Setups: Wii-Spiele nativ, GameCube über Nintendont, N64 über VC-Inject/Not64/Wii64, NES/SNES/GB/GBC/GBA über Emulatoren – sortiert nach Konsolengeneration.

- Design: [docs/superpowers/specs/2026-11-09-wii-launcher-design.md](docs/superpowers/specs/2026-11-09-wii-launcher-design.md)
- Pläne: [docs/superpowers/plans/](docs/superpowers/plans/)
- Lizenz: GPLv3 ([LICENSE](LICENSE))

## Voraussetzungen (Windows)

- **devkitPro** mit Komponente „Wii Development“ (`C:\devkitPro`), danach in dessen MSYS2-Shell:
  `pacman -S --needed wii-dev cmake ppc-freetype`
- **Host-Compiler:** `winget install --id BrechtSanders.WinLibs.POSIX.UCRT -e`
- **Ninja:** `winget install --id Ninja-build.Ninja -e`
- **CMake** ≥ 3.25 und **Python 3**
- **Dolphin** unter `../wii_tools/Dolphin-x64/` (neben diesem Repo)

## Befehle

Host-Unit-Tests (plattformfreier Kern):
```bash
cmake --preset host && cmake --build --preset host && ctest --preset host
```

Werkzeug-Tests:
```bash
python -m unittest discover -s tools/tests -v
```

Wii-Build (`build/wii/newlauncher.dol` und Selbsttest `build/wii/nl_selftest.dol`):
```bash
python tools/build_wii.py
```

DOL in Dolphin starten (eigener Dev-User-Ordner, private Dolphin-Konfiguration bleibt unberührt):
```bash
../wii_tools/Dolphin-x64/Dolphin.exe -u ../wii_tools/dolphin-user -e build/wii/newlauncher.dol
```

Auf die virtuelle SD deployen (Launcher + Fixture-Bibliothek aus `tests/fixtures/sd/`) bzw. Smoke-Test (wartet auf `NL_READY` im Dolphin-Log; `--no-fixtures` lässt die SD wie sie ist):
```bash
python tools/deploy_dolphin.py
python tools/dolphin_smoke.py
```

Selbsttest in Dolphin (geskriptete Eingaben, keine Allokation im Leerlauf, absichtlicher Absturz am Ende):
```bash
python tools/dolphin_smoke.py --dol build/wii/nl_selftest.dol --marker "NL_LIBRARY games=12" --marker NL_FAV_SAVED --marker "NL_SEARCH mario=5" --marker "NL_OVERRIDE_SAVED n64/Super Mario 64 (Europe) (En,Fr,De).z64 not64" --marker "NL_PREFLIGHT_FAIL Unsupported GM8P01" --marker "NL_VC_CHANNEL 000100014E4C5443 NL Testkanal" --marker "NL_VCMAP_SAVED n=2" --marker "NL_SELFTEST_COVERS" --marker "NL_ALLOC_IDLE 0" --marker NL_SELFTEST_OK --marker NL_SELFTEST_LOG_SAVED --marker "NL_CRASH id=7" --marker "NL_LANG en" --marker NL_CONFIG_SAVED
```

Klänge und Musik in Dolphin (UI-Klänge melden sich mit `[debug] log_sounds = 1` als `NL_SOUND`; die Fixtures legen `tone.ogg` und die kaputte `header_only.ogg` in den Musikordner; beim Spielstart stoppt der Ton vor dem Chainload):
```bash
python tools/dolphin_smoke.py --marker "NL_AUDIO ok" --marker NL_READY
```
```bash
python tools/dolphin_smoke.py --marker NL_READY --marker "NL_MUSIC play sd:/newlauncher/music/tone.ogg rate=32000 ch=1" --marker "NL_MUSIC_FAIL sd:/newlauncher/music/header_only.ogg"
```
```bash
python tools/dolphin_smoke.py --autostart "nes/Super Mario Bros. (World).nes" --marker NL_AUDIO_STOP --marker "ARGV 1"
```

Chainload-Test in Dolphin (startet über `[debug] autostart` ein NES-Spiel der Fixtures; die Test-DOL `argv_echo` meldet die übergebenen Argumente):
```bash
python tools/dolphin_smoke.py --autostart "nes/Zelda (Europe) (Rev 1).nes" --marker "NL_LAUNCH nes/Zelda (Europe) (Rev 1).nes" --marker "NL_ARGV argc=3" --marker "NL_ARGV 0 sd:/apps/argv_echo/boot.dol" --marker "NL_ARGV 1 sd:/roms/nes/" --marker "NL_ARGV 2 Zelda (Europe) (Rev 1).nes"
```

N64 in Dolphin: Not64-Weg (die Fixtures setzen `[n64_not64]` auf `argv_echo`) und VC-Weg über den Testkanal (dessen Boot-Content ist ebenfalls `argv_echo`):
```bash
python tools/dolphin_smoke.py --autostart "n64/Super Mario 64 (Europe) (En,Fr,De).z64" --marker "NL_N64 n64/Super Mario 64 (Europe) (En,Fr,De).z64 not64" --marker "NL_ARGV argc=2" --marker "NL_ARGV 0 sd:/apps/argv_echo/boot.dol" --marker "NL_ARGV 1 sd:/roms/n64/Super Mario 64 (Europe) (En,Fr,De).z64"
```
```bash
python tools/dolphin_smoke.py --autostart "n64/Mario Kart 64 (Europe).z64" --marker "NL_N64 n64/Mario Kart 64 (Europe).z64 vc" --marker "NL_LAUNCH_VC 000100014E4C5443" --marker "NL_ARGV argc="
```

Cover in Dolphin: das Fixture-PNG wird auf der Wii nach CMPR umgewandelt und im Cache abgelegt; `--cover-server` startet zusätzlich einen lokalen HTTP-Server (127.0.0.1, freier Port) mit zwei Test-Covern und löst über `[debug] cover_url` und `fetch_covers` „Fehlende Cover laden“ aus:
```bash
python tools/dolphin_smoke.py --marker "NL_COVER_CONVERTED nes/Super Mario Bros. (World)" --marker "NL_COVERS ready="
```
```bash
python tools/dolphin_smoke.py --cover-server --marker "NL_NET ok" --marker "NL_COVER_DL RMCP01 EN ok" --marker "NL_COVER_DL GM8P01 US ok" --marker "NL_COVER_DL RMGP01 missing" --marker "NL_COVER_DL_DONE ok=2 missing=1 failed=0"
```

Emulator-Installation in Dolphin: `--emu-server` startet ein lokales Test-Repository im OSC-Format (eine App `fceugx`, deren `boot.dol` die Test-DOL `argv_echo` ist), löscht `apps/fceugx` im SD-Sync-Ordner und stellt `[nes] dol` dort auf `sd:/apps/fceugx/boot.dol`. Über `[debug] emu_repo` und `emu_install` lädt der Launcher nach `NL_READY` die Liste, installiert `fceugx` und startet erst danach das Autostart-Spiel:
```bash
python tools/dolphin_smoke.py --emu-server --autostart "nes/Super Mario Bros. (World).nes" --timeout 120 --marker "NL_EMU_LIST ok" --marker "NL_EMU_INSTALL fceugx ok" --marker "ARGV 1"
```

Die CI baut `newlauncher.dol`, `nl_selftest.dol` und `argv_echo.dol` und lädt sie als Artefakte hoch.

Schriften neu reduzieren (nur bei Schriftwechsel, braucht `pip install fonttools`):
```bash
python tools/subset_fonts.py MPLUSRounded1c-Regular.ttf MPLUSRounded1c-Medium.ttf
```

Fixture-Bibliothek neu erzeugen (nur Dummy-Header und leere ROM-Dateien, keine Spiele):
```bash
python tools/make_fixtures.py tests/fixtures/sd
```

## Dateien auf SD und Bibliotheksgerät

`<lib>` ist `usb` oder `sd` (Einstellung `library` in `config.ini`).

```
sd:/apps/newlauncher/        boot.dol, meta.xml
sd:/newlauncher/config.ini   Einstellungen (wird beim ersten Start mit Standardwerten angelegt)
sd:/newlauncher/emulators.ini   Emulator-Pfade und Argumente (wird beim ersten Start angelegt)
sd:/newlauncher/n64_vc.ini   N64: Zuordnung Spiel → VC-Inject-Kanal (beim ersten Speichern angelegt)
sd:/newlauncher/games.ini    N64: Startmethode je Spiel (beim ersten Speichern angelegt)
sd:/newlauncher/favorites.ini, history.ini, log.txt
sd:/newlauncher/music/*.ogg   Hintergrundmusik (Ogg Vorbis)
<lib>:/newlauncher/cache/index.bin   Spiele-Index (vom Launcher geschrieben)
<lib>:/newlauncher/cache/meta.bin    Metadaten (prepare_meta.py)
<lib>:/newlauncher/covers_src/<stamm>.png   Cover-Quellen (prepare_covers.py bzw. „Fehlende Cover laden“)
<lib>:/newlauncher/cache/covers/<stamm>.tex Cover als CMPR-Textur (vom Launcher erzeugt)
<lib>:/wbfs/Titel [ID6]/ID6.wbfs|.iso    Wii
<lib>:/games/Titel [ID6]/game.iso        GameCube
<lib>:/roms/nes|snes|gb|gbc|gba|n64/     ROMs
```

## Cover und Metadaten

Metadaten (Titel, Jahr, Hersteller, Genre) erzeugt ein PC-Skript als `<lib>/newlauncher/cache/meta.bin`. Wii/GC-Titel kommen aus `wiitdb.xml` von [GameTDB](https://www.gametdb.com/) (Download „wiitdb.zip“, entpacken), Retro-Titel aus den No-Intro-DATs von libretro (lädt das Skript selbst, `--offline` verzichtet darauf, `--dat-dir` nimmt eigene DATs):
```bash
python tools/prepare_meta.py --wiitdb wiitdb.xml --lang DE E:/
```
`--lang` wählt die Sprache der Wii/GC-Titel (`EN`, `DE`, `FR`, `JA`, …); fehlt sie in der Datenbank, gilt Englisch.

Retro-Boxarts lädt ein zweites Skript von libretro-thumbnails nach `<lib>/newlauncher/covers_src/` (vorhandene PNGs bleiben, `--force` lädt neu):
```bash
python tools/prepare_covers.py E:/
```

Wii/GC-Cover lädt der Launcher selbst: HOME-Menü → Einstellungen → „Fehlende Cover laden“ holt für alle Wii/GC-Spiele ohne Cover das PNG von GameTDB. Die Region kommt aus der Einstellung „Cover-Region“ (`[covers] region` in `config.ini` (`auto` = Systemsprache, sonst `EN`, `DE`, `FR`, `US`, `JA`), mit Rückfall auf `EN`, `US` und `JA`. Der Fortschritt steht unten als Hinweis, am Ende fasst ein Dialog zusammen.

Der Launcher wandelt jedes PNG beim ersten Anzeigen in eine CMPR-Textur unter `<lib>/newlauncher/cache/covers/` um. Wer ein Cover in `covers_src` austauscht, löscht danach `cache/covers` (oder nur die passende `.tex`), sonst bleibt die alte Textur.

Nur für Tests: `[debug] perf = 1` schreibt die `NL_PERF`-Messzeilen (siehe [Auf der Wii testen](#auf-der-wii-testen)), `[debug] cover_url` ersetzt die GameTDB-Adresse, `[debug] fetch_covers = 1` löst „Fehlende Cover laden“ nach `NL_READY` aus, `[debug] log_sounds = 1` meldet jeden gespielten UI-Klang als `NL_SOUND`, `[debug] emu_repo` ersetzt `[emulators] repo`, `[debug] emu_install = <name>,<name>` lädt nach `NL_READY` die Liste und installiert diese Emulatoren (`[debug] autostart` wartet darauf).

## Emulatoren

NES, SNES, GB, GBC und GBA starten über Emulator-DOLs von der SD. Der Launcher lädt die DOL, übergibt die ROM als `argv` und springt per Chainloader hinein. `sd:/newlauncher/emulators.ini` legt je Konsole die DOL und das Argument-Schema fest. Fehlt die Datei, wird sie mit diesen Standardwerten angelegt:

| Konsole | DOL | `args` |
|---|---|---|
| `nes` | `sd:/apps/fceugx/boot.dol` | `{dir}\|{file}` |
| `snes` | `sd:/apps/snes9xgx/boot.dol` | `{dir}\|{file}` |
| `gb`, `gbc`, `gba` | `sd:/apps/vbagx/boot.dol` | `{dir}\|{file}` |
| `n64_not64` | `sd:/apps/not64/boot.dol` | `{path}` |
| `n64_wii64` | `sd:/apps/wii64/boot.dol` | `{path}` |

`args` ist eine durch `|` getrennte Liste; jeder Eintrag wird ein `argv`-Element hinter `argv[0]` (immer der DOL-Pfad). Platzhalter:
- `{dir}`: Ordner der ROM mit abschließendem `/`
- `{file}`: Dateiname
- `{path}`: vollständiger Pfad

Fehlende Einträge fallen auf den Standard zurück. GameCube und Wii folgen mit M5; bis dahin zeigt „Starten“ dort einen Hinweis.

Vor dem Start prüft der Launcher, ob die DOL existiert und gültig ist und ob die ROM vorhanden ist. Fehlt etwas, erscheint ein Dialog, und der Launcher läuft weiter.

### Installieren und aktualisieren

HOME-Menü → Einstellungen → „Emulatoren“ zeigt je Emulator (FCE Ultra GX, Snes9x GX, Visual Boy Advance GX, Not64, Wii64, Nintendont) den Stand: nicht installiert, aktuell, Update verfügbar oder Version unbekannt. Beim Öffnen geht nichts ins Netz; erst „Nach Updates suchen“ lädt die Liste des Open Shop Channel. A auf einem Emulator installiert bzw. aktualisiert ihn, „Alle aktualisieren“ nimmt alle mit Update oder unbekannter Version. B bricht einen laufenden Download ab.

Die Quelle steht in `config.ini`:

```ini
[emulators]
repo = http://hbb1.oscwii.org/hbb
```

Der Launcher lädt das ZIP nach `sd:/newlauncher/tmp/`, prüft Größe und CRC, entpackt dort und verschiebt erst danach: Dateien unter `apps/` werden ersetzt, alle anderen (z. B. Emulator-Einstellungen) nur angelegt, wenn sie fehlen; `boot.dol` kommt zuletzt. Stand und Version merkt sich `sd:/newlauncher/emulators_installed.ini`. Reicht der Platz auf der SD nicht für ZIP plus entpackten Inhalt, startet der Download nicht. Bisher nur über HTTP; HTTPS folgt mit dem TLS-Modul (Spec 6.9).

### N64

Ein N64-Spiel startet auf einem von drei Wegen. Ohne eigene Wahl nimmt der Launcher den ersten verfügbaren:
1. **VC-Inject**: Das Spiel ist in `n64_vc.ini` einem Kanal zugeordnet, und der Kanal ist installiert. Der Launcher startet den Kanal direkt.
2. **Not64**: Die DOL aus `[n64_not64]` existiert.
3. **Wii64**: Die DOL aus `[n64_wii64]` existiert.

Gibt es keinen Weg, erscheint beim Start ein Hinweis (`NoN64Path`).

In den Details eines N64-Spiels führt Hoch auf die Zeile „Startmethode“. Links und Rechts wechseln dort zwischen „Automatisch“ und den verfügbaren Wegen. Die Wahl wird sofort in `games.ini` gespeichert. Ist ein gewählter Weg später nicht mehr verfügbar, gilt wieder die Auto-Wahl, und die Details zeigen den Grund.

```ini
; sd:/newlauncher/games.ini
; Werte: vc | not64 | wii64 (Kommentare nur als ganze Zeile)
[n64_method]
n64/Super Mario 64 (Europe).z64 = not64

; sd:/newlauncher/n64_vc.ini (Title-ID als 16 Hex-Ziffern)
[map]
n64/Mario Kart 64 (Europe).z64 = 000100014E4B5450
```

Zuordnen lässt sich auch im Launcher: HOME → „Einstellungen“ → „VC-Injects zuordnen“.
- Er listet alle N64-Spiele.
- Links und Rechts wählen einen der installierten Kanäle oder „keiner“.
- A übernimmt den Namensvorschlag.
- B speichert und schließt.

Kanäle zeigt der Dialog mit ihrem Bannernamen, ist der nicht lesbar, mit der Title-ID.

In Dolphin legen `deploy_dolphin.py` und `dolphin_smoke.py` einen Testkanal „NL Testkanal“ (`000100014E4C5443`) im Dev-NAND `../wii_tools/dolphin-user/Wii/` an. Sein Boot-Content ist die Test-DOL `argv_echo`. Die Fixtures ordnen ihm „Mario Kart 64“ zu.

## Bedienung

| Aktion | Wiimote | Nunchuk | GameCube-Controller |
|---|---|---|---|
| Navigieren | Steuerkreuz | Stick | Stick, Steuerkreuz |
| Zeigen | IR-Zeiger | – | – |
| Bestätigen | A | – | A |
| Zurück | B | – | B |
| Konsole wechseln | − / + | – | L / R |
| Favorit umschalten | 1 | – | Y |
| Suche / A–Z | 2 | – | X |
| HOME-Menü | HOME | – | Start |
| Debug-Overlay | 1 + 2 | – | Z + Start |

- **A–Z:** Taste „Suche / A–Z“ öffnet die Sprungleiste; ein Buchstabe springt zum ersten Titel (ohne Treffer zum nächsten Buchstaben), „Suche“ öffnet die Bildschirmtastatur.
- **Suche:** filtert live über alle Konsolen, ohne Rücksicht auf Groß/klein, Akzente und No-Intro-Tags; B beendet den Suchmodus.
- **Details:** A auf einem Spiel; „Starten“ startet den Emulator (siehe [Emulatoren](#emulatoren)), Favorit per Knopf oder Taste 1.

Das Zahnrad unten rechts schaltet die Darstellung für die laufende Sitzung um (Hell → Dunkel → Automatisch); dauerhaft legt sie die Einstellung „Darstellung“ fest.

## Einstellungen

HOME → „Einstellungen“ öffnet die Liste. Hoch und Runter wählen eine Zeile, Links und Rechts ändern den Wert, A führt eine Aktion aus, B schließt und speichert nach `sd:/newlauncher/config.ini`. Sprache, Darstellung, Klänge und Musik wirken sofort; das Bibliotheksgerät erst nach einem Neustart.

| Einstellung | Werte | Standard | `config.ini` |
|---|---|---|---|
| Sprache | Automatisch (Systemsprache), Deutsch, Englisch | Automatisch | `[general] language = auto\|de\|en` |
| Darstellung | Hell, Dunkel, Automatisch nach Uhrzeit | Hell | `[theme] mode = light\|dark\|auto` |
| Dunkel von / bis | Uhrzeit | 19:00 / 07:00 | `[theme] dark_from`, `dark_to` (`HH:MM`) |
| Musik | an/aus, Lautstärke 0–100 | an, 60 | `[audio] music = 0\|1`, `music_volume` |
| UI-Klänge | an/aus | an | `[audio] sounds = 0\|1` |
| Bibliotheksgerät | USB, SD | USB | `[general] library = usb\|sd` |
| Cover-Region | Automatisch, EN, DE, FR, US, JA | Automatisch | `[covers] region` |

Aktionen am Ende der Liste: „Bibliothek neu scannen“, „Fehlende Cover laden“ (siehe [Cover und Metadaten](#cover-und-metadaten)) und „VC-Injects zuordnen“ (siehe [N64](#n64)).

### Musik

Der Launcher spielt alle `.ogg`-Dateien (Ogg Vorbis, mono oder stereo) aus `sd:/newlauncher/music/` in zufälliger Reihenfolge; nach jeder Runde wird neu gemischt, ohne dass ein Stück direkt zweimal läuft. Ist der Ordner leer oder fehlt er, bleibt es still. Nicht lesbare Dateien werden übersprungen; scheitern alle Stücke einer Runde nacheinander, schaltet sich die Musik für die Sitzung ab (`NL_MUSIC_OFF`). Vor jedem Spielstart, Kanalstart und beim Verlassen stoppt der Ton.

## Auf der Wii testen

Die Abnahme auf echter Hardware folgt der Checkliste [docs/hardware-acceptance.md](docs/hardware-acceptance.md). Auf der Wii ist `SYS_Report` stumm; alle Marker landen nur in `sd:/newlauncher/log.txt` (die letzten 16 KB, geschrieben bei `NL_READY`, vor jedem Spielstart und beim Beenden).

Release-DOL auf die SD-Karte (Laufwerk `E:`), Messung an:
```bash
python tools/wii_deploy.py E: --perf on
```
Kopiert `boot.dol` und `meta.xml` nach `apps/newlauncher/`. Unter `newlauncher/` wird nichts überschrieben; `--perf on|off` setzt bzw. entfernt nur `[debug] perf` in `config.ini`. Eine Karte ohne `apps/`-Ordner lehnt das Skript ab, außer mit `--init`.

Selbsttest-DOL auf eine eigene, leere SD-Karte:
```bash
python tools/wii_deploy.py F: --selftest
```
Die Selbsttest-DOL spielt ein festes Eingabeskript gegen die Fixture-Bibliothek und schreibt dabei Favoriten, Verlauf, Einstellungen und VC-Zuordnungen. Auf einer echten Karte überschriebe sie diese Daten; deshalb verweigert das Skript jede Karte mit `wbfs/`, `games/`, `roms/` oder `newlauncher/`. Am Ende sichert sie `log.txt` (`NL_SELFTEST_LOG_SAVED`) und stürzt absichtlich ab, um den Absturzbildschirm zu zeigen.

Ohne Kartenwechsel per WLAN an den offenen Homebrew Channel (`wiiload` aus devkitPro):
```bash
python tools/wii_deploy.py --wiiload 192.168.0.42
```

Auswertung der Messung (mehrere Logs werden zusammengeführt, z. B. Release- und Selbsttest-Karte); gibt eine Markdown-Tabelle aus, Exit 1 bei einem verfehlten Budget:
```bash
python tools/check_perf.py E:\newlauncher\log.txt F:\newlauncher\log.txt
```

Mit `[debug] perf = 1` schreibt der Launcher:

| Marker | Bedeutung |
|---|---|
| `NL_PERF boot first_frame_ms=<n> ready_ms=<n> usb_wait_ms=<n> hz=<50\|60>` | einmal nach `NL_READY`: Zeit bis zum ersten Bild und bis bedienbar, davon USB-Wartezeit |
| `NL_PERF frames n=<n> slow=<n> max_ms=<x.y> busy=<0\|1>` | alle 300 Frames: Frames über 1,5 Bildperioden; `busy=1`, wenn Scan, Cover-Download oder Cover-Umwandlung lief |
| `NL_PERF summary ready_ms=… usb_wait_ms=… hz=… frames=… slow=… busy_slow=… max_ms=… cover_n=… cover_p50=… cover_p95=… cover_max=… mem1_free=… mem2_free=… heap_free=…` | vor jedem Log-Schreiben beim Spielstart und Beenden: alle Budget-Werte der Sitzung; Cover-Latenz vom ersten sichtbaren Wunsch bis zur fertigen Textur (ohne PNG-Umwandlung) |

## Log-Marker (Dolphin-Log, OSREPORT)

| Marker | Bedeutung |
|---|---|
| `NL_READY` | erster Frame gezeichnet |
| `NL_ATLAS ok` | Schrift-Atlas aufgebaut |
| `NL_ERROR <text>` | Start abgebrochen |
| `NL_CMD <name>` | Befehl aus dem HOME-Menü, dem Zahnrad oder den Einstellungen (`exit_hbc`, `wii_menu`, `open_vc_map`, `fetch_covers`, `power_off`, `cycle_theme`, `settings_changed`, `save_settings`, `rescan`) |
| `NL_ALLOC_IDLE <n>` | Selbsttest: Allokationen in 120 Leerlauf-Frames |
| `NL_SELFTEST_OK` | Selbsttest bestanden |
| `NL_SELFTEST_LOG_SAVED` | Selbsttest: `log.txt` vor dem absichtlichen Absturz geschrieben |
| `NL_PERF boot\|frames\|summary …` | Messzeilen mit `[debug] perf = 1`, siehe [Auf der Wii testen](#auf-der-wii-testen) |
| `NL_PANIC <ausdruck> <datei>:<zeile>` | verletztes `NL_ASSERT` |
| `NL_CRASH id=<n> pc=… lr=…` | CPU-Ausnahme, Absturzbildschirm aktiv |
| `NL_CONFIG ok\|default` | `config.ini` gelesen bzw. mit Standardwerten angelegt |
| `NL_MOUNT <lib>` | Bibliotheksgerät (`sd:` oder `usb:`) |
| `NL_LIBRARY games=<n> source=<index\|scan>` | Bibliothek bereit |
| `NL_SCAN_DONE games=<n> added=<a> removed=<r>` | Scan fertig (erster oder Hintergrund-Scan) |
| `NL_INDEX_SAVED` | `index.bin` geschrieben |
| `NL_FAV_SAVED <schlüssel>` | Favoriten gespeichert |
| `NL_SEARCH mario=<n>` | Selbsttest: Treffer der Suche „MARIO“ |
| `NL_EMULATORS ok\|default` | `emulators.ini` gelesen bzw. Standardwerte genutzt |
| `NL_PREFLIGHT_FAIL <art> <schlüssel>` | Start abgelehnt (`Unsupported`, `EmuMissing`, `EmuInvalid`, `RomMissing`, `ArgsTooLong`, `NoMemory`, `NoN64Path`), Dialog offen |
| `NL_NAND channels=<n>` | beim Start: Anzahl installierter Kanäle |
| `NL_VC_CHANNELS n=<n>`, `NL_VC_CHANNEL <titleid> <name>` | Zuordnungsdialog geöffnet: Kanalliste |
| `NL_OVERRIDE_SAVED <schlüssel> <auto\|vc\|not64\|wii64>` | Startmethode in `games.ini` gespeichert |
| `NL_VCMAP_SAVED n=<anzahl>` | `n64_vc.ini` gespeichert |
| `NL_N64 <schlüssel> <vc\|not64\|wii64>` | gewählter N64-Weg beim Start |
| `NL_LAUNCH_VC <titleid>` | Kanalstart beginnt |
| `NL_HISTORY_SAVED` | Verlauf vor dem Start gespeichert |
| `NL_LAUNCH <schlüssel>` | Chainload beginnt |
| `NL_AUTOSTART <schlüssel>` | Testmodus: `[debug] autostart` in `config.ini` startet das Spiel nach `NL_READY` |
| `NL_ARGV argc=<n>`, `NL_ARGV <i> <wert>` | Test-DOL `argv_echo`: empfangene Argumente |
| `NL_COVER_CONVERTED <stamm>` | PNG in CMPR-Textur umgewandelt und im Cache abgelegt |
| `NL_COVER_FAIL <stamm> <grund>` | Cover nicht lesbar (Platzhalter bleibt) |
| `NL_COVERS ready=<n>` | Cover-Thread untätig: Anzahl geladener Cover |
| `NL_SELFTEST_COVERS ready=<n>` | Selbsttest: geladene Cover vor dem Leerlauf-Fenster |
| `NL_NET ok`, `NL_NET_FAIL <code>` | Netzwerk für „Fehlende Cover laden“ bereit bzw. nicht verfügbar |
| `NL_COVER_DL_START jobs=<n>` | „Fehlende Cover laden“: Anzahl fehlender Wii/GC-Cover |
| `NL_COVER_DL <id6> <region> ok`, `NL_COVER_DL <id6> missing\|failed` | Ergebnis je Spiel |
| `NL_COVER_DL_ABORT failures=3` | drei Fehler in Folge: übrige Spiele werden nicht mehr versucht (`failed`) |
| `NL_COVER_DL_DONE ok=<n> missing=<n> failed=<n>` | Download fertig, Dialog offen |
| `NL_EMU_LIST ok n=<n>`, `NL_EMU_LIST fail` | OSC-Liste geladen (Anzahl Apps) bzw. nicht geladen (bei fehlendem Netz vorher `NL_NET_FAIL <code>`) |
| `NL_EMU_INSTALL <name> ok files=<n>` | Emulator installiert bzw. aktualisiert |
| `NL_EMU_INSTALL <name> fail <nospace\|download\|badsize\|badzip\|write\|cancelled> <detail>` | Installation fehlgeschlagen; `tmp/` ist aufgeräumt, die alte Version bleibt |
| `NL_EMU_DEBUG_INSTALL n=<n>` | Testmodus: so viele Apps aus `[debug] emu_install` stehen in der Liste |
| `NL_EMU_DONE not64=<0\|1> wii64=<0\|1>` | Installationen fertig; N64-DOLs neu geprüft |
| `NL_LANG <de\|en>` | aktive Sprache (beim Start und nach Änderung) |
| `NL_CONFIG_SAVED` | Einstellungen in `config.ini` gespeichert |
| `NL_RESCAN start` | „Bibliothek neu scannen“ gestartet |
| `NL_BATTERY p1=<n> p2=<n> p3=<n> p4=<n>` | HOME-Menü geöffnet: Batteriestand je Wiimote (0–4, −1 = nicht verbunden) |
| `NL_AUDIO ok`, `NL_AUDIO_FAIL <grund>` | Klänge bereit bzw. nicht verfügbar |
| `NL_AUDIO_STOP` | Ton vor Spielstart oder Beenden gestoppt |
| `NL_SOUND <move\|confirm\|back\|open\|error>` | gespielter UI-Klang (nur mit `[debug] log_sounds = 1`) |
| `NL_MUSIC play <pfad> rate=<hz> ch=<n>` | neues Musikstück |
| `NL_MUSIC_FAIL <pfad>`, `NL_MUSIC_OFF` | Stück nicht abspielbar bzw. Musik abgeschaltet |
| `NL_WARN <text>` | nicht kritischer Fehler (z. B. kaputter Cache verworfen) |

## Schriften

M PLUS Rounded 1c (SIL Open Font License 1.1, siehe `assets/fonts/OFL.txt`), auf Latin-1 plus „…" reduziert.
