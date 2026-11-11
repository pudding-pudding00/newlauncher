# newlauncher – Design-Spezifikation

- **Datum:** 2026-11-09
- **Status:** Entwurf zur Freigabe
- **Repo:** `pudding-pudding00/newlauncher`
- **Entwicklungsumgebung:** Windows 11, Dolphin unter `../wii_tools/Dolphin-x64/`

---

## 1. Ziel

Ein hochperformanter, moderner Launcher im Wii-Stil für eine Wii mit Homebrew Channel (HBC) und d2x-cIOS.
Er zeigt Spiele nach Konsolengeneration sortiert und startet sie je nach Konsole auf dem passenden Weg:

| Konsole | Startweg |
|---|---|
| Wii | nativ von USB (cIOS, Disc-Emulation) |
| GameCube | Nintendont |
| N64 | VC-Inject-Kanal, Not64 oder Wii64 – automatische Wahl, pro Spiel überschreibbar |
| NES, SNES, GB, GBC, GBA | Homebrew-Emulator (DOL) mit ROM-Pfad als Argument |

### 1.1 Rahmenbedingungen

- **Zweck:** eigener Gebrauch. Feste Annahmen (eine Ordnerstruktur, ein cIOS-Setup) sind erlaubt.
- **Plattform:** nur originale Wii.
- **Testhardware:** vorerst nur Dolphin; die echte Wii folgt später (ab Meilenstein M7).
- **Speicher:** USB-Festplatte, FAT32, Szene-Standardordner. In Dolphin ersetzt die virtuelle SD-Karte das USB-Gerät.

### 1.2 Nicht-Ziele (v1)

- vWii (Wii U), Wii mini
- NTFS/ext2, WBFS-Partitionen (nur `.wbfs`/`.iso`-Dateien auf FAT32)
- EmuNAND
- Classic Controller (Eingabeschicht ist so gebaut, dass er später ergänzt werden kann)
- Sega, PlayStation, weitere Exoten-Konsolen
- Start von Disc im Laufwerk
- Cheats/Ocarina, Kindersicherung, Mehrbenutzer, Fremd-Themes, Forwarder-Kanal
- Spieloptionen für Wii/GC (Videomodus, Sprache usw.) – werden mit M8/M9 spezifiziert

---

## 2. Entscheidungsübersicht

| Thema | Entscheidung |
|---|---|
| Architektur | Eigenes Frontend + portierter Boot-Kern (WiiFlow Lite / USB Loader GX) in gekapseltem Modul |
| Sprache | C++20 ohne Exceptions und RTTI; portierter Boot-Code bleibt C |
| Rendering | Eigener schlanker GX-Renderer |
| Toolchain | devkitPro (devkitPPC + libogc) nativ unter Windows, CI über GitHub Actions |
| Build-System | CMake für Wii-Ziel und Host-Tests |
| Host-Compiler | MinGW-w64 GCC über WinLibs (winget) + Ninja |
| Tests | Host-Unit-Tests (doctest) + automatischer Dolphin-Smoke-Test |
| Konsolen | Wii, GC, N64, NES, SNES, GB, GBC, GBA |
| Navigation | Konsolen-Leiste oben, chronologisch nach Generation gruppiert |
| N64-Startweg | Auto-Wahl pro Spiel (VC-Inject → Not64 → Wii64), pro Spiel überschreibbar |
| VC-Injects | Auf echtem NAND installiert, Verknüpfung per Mapping-Datei + Namensvorschlag |
| Cover | Wii/GC: Download auf der Wii (GameTDB); Retro: PC-Skript (libretro-thumbnails) |
| Emulatoren | Installation und Update auf der Wii aus dem Open Shop Channel (HTTP), manuell ausgelöst; derselbe Weg am PC über `wii_deploy.py --emulators` |
| Metadaten | PC-Skript erzeugt kompakte `meta.bin` (GameTDB + libretro-Datenbanken) |
| Eingabe | Wiimote (Pointer + Steuerkreuz), Nunchuk-Stick, GameCube-Controller |
| Komfort v1 | Favoriten, Zuletzt gespielt, A–Z-Sprung + Suche, UI-Sounds + Musik, Detailansicht |
| HOME-Taste | HOME-Menü-Overlay mit Optionen |
| UI-Sprache | Deutsch + Englisch, Standard folgt Wii-Systemsprache |
| Look | Layout „A – Klassisch Wii“, Spielinfo in unterer Leiste, Hell- und Dunkelmodus |
| Datenablage | Konfiguration auf SD; Index-Cache, Metadaten und Cover beim Bibliotheksgerät |
| Lizenz | GPLv3 (wegen portiertem Boot-Code) |

---

## 3. Oberfläche

### 3.1 Hauptbildschirm

```
┌──────────────────────────────────────────────────────────────────┐
│ ★Favoriten  ◷Zuletzt  Wii  GC  GBA  [N64]  GBC  SNES  GB  NES      │  ← Konsolen-Leiste
├──────────────────────────────────────────────────────────────────┤
│  ┌───┐ ┌───┐ ┌───┐ ┌───┐ ┌───┐ ┌───┐                              │
│  │   │ │▓▓▓│ │   │ │   │ │   │ │   │   Cover-Raster, 6 Spalten     │
│  └───┘ └───┘ └───┘ └───┘ └───┘ └───┘   Fokus = blauer Rahmen       │
│  ┌───┐ ┌───┐ ┌───┐ ┌───┐ ┌───┐ ┌───┐                              │
│  └───┘ └───┘ └───┘ └───┘ └───┘ └───┘                              │
├──────────────────────────────────────────────────────────────────┤
│ (⌂)   The Legend of Zelda: Ocarina of Time            19:42  (⚙) │  ← Info-Leiste
│       N64 · 1998 · 1 Spieler · Start: VC-Inject                   │
└──────────────────────────────────────────────────────────────────┘
```

- **Konsolen-Leiste:** Einträge „Favoriten“ und „Zuletzt“, danach nur die Konsolennamen von neu nach alt (Generation absteigend, innerhalb einer Generation Heimkonsole vor Handheld), ohne Generationsbeschriftung. Konsolen ohne Spiele werden ausgeblendet.
- **Cover-Raster:** 6 Spalten, vertikal scrollend, Fokus mit Akzentrahmen und leichter Vergrößerung (Animation).
- **Info-Leiste (unten):** links runder HOME-Knopf, Mitte Titel und Infozeile des fokussierten Spiels (Konsole · Jahr · Spieler · ermittelte Startmethode), rechts Uhrzeit und runder Einstellungs-Knopf.
- Ohne Fokus auf einem Spiel zeigt die Mitte der Leiste das Datum.

### 3.2 Spieldetails

Geöffnet mit A oder Klick auf ein fokussiertes Cover.

- Links großes Cover, rechts Titel, Infozeile (Konsole · Generation · Jahr · Spieler · zuletzt gespielt), Beschreibung (max. 3 Zeilen).
- **Startmethode** (nur N64): Auswahl „Automatisch (→ ermittelter Weg)“ / VC-Inject / Not64 / Wii64. Nicht verfügbare Wege sind ausgegraut, mit Begründung. Eine feste Wahl speichert einen Override in `games.ini`; „Automatisch“ entfernt ihn.
- Knöpfe: **Starten** (Akzent), **Favorit**. Ein Knopf **Optionen** erscheint erst, wenn es für die Konsole Spieloptionen gibt (Wii/GC ab M8/M9).
- Info-Leiste zeigt Tastenhilfe („A Starten · B Zurück · 1 Favorit“) und Uhrzeit.

### 3.3 HOME-Menü

Overlay über der abgedunkelten aktuellen Szene, Ein-/Ausblend-Animation.

- Kopfleiste „HOME-Menü“ mit Akzentlinie.
- Vier Kacheln: **Homebrew Channel**, **Wii-Menü**, **Einstellungen**, **Ausschalten**.
- Fußleiste: Batteriestand der verbundenen Wiimotes (P1–P4), Hinweis „HOME schließt“.

### 3.4 Einstellungen

| Einstellung | Werte | Standard |
|---|---|---|
| Sprache | Automatisch, Deutsch, Englisch | Automatisch |
| Darstellung | Hell, Dunkel, Automatisch nach Uhrzeit | Hell |
| Dunkel von / bis | Uhrzeit | 19:00 / 07:00 |
| Musik | an/aus, Lautstärke 0–100 | an, 60 |
| UI-Sounds | an/aus | an |
| Bibliotheksgerät | USB, SD | USB |
| Cover-Region | Automatisch, EN, DE, FR, US, JA | Automatisch |
| Aktionen | „Bibliothek neu scannen“, „Fehlende Cover laden“, „VC-Injects zuordnen“, „Emulatoren“ (6.9) | – |

### 3.5 Suche und A–Z-Sprung

- **A–Z-Sprung:** Die Taste „Suche / A–Z“ öffnet eine Sprungleiste mit `#`, `A`–`Z` und dem Eintrag „Suche“. Ein Buchstabe springt zum ersten Titel mit diesem Anfangsbuchstaben in der aktuellen Konsole.
- **Suche:** Der Eintrag „Suche“ öffnet die Bildschirmtastatur (Pointer oder Steuerkreuz). Sie filtert live über alle Konsolen; Groß-/Kleinschreibung und Akzente werden ignoriert, No-Intro-Tags zählen nicht.

### 3.6 Theme

Ein Theme ist eine Tabelle von Farb-Tokens. Umschalten tauscht die Tabelle; ein Übergang interpoliert 200 ms zwischen den Werten. Icons liegen als einfarbige Alpha-Texturen (IA8) vor und werden beim Zeichnen eingefärbt.

| Token | Hell | Dunkel |
|---|---|---|
| `bg` | `#EEF1F4` | `#121821` |
| `bar` | `#FFFFFF` | `#1A2230` |
| `barBorder` | `#D5DBE1` | `#2C3A4E` |
| `tile` | `#FFFFFF` | `#243041` |
| `tileFocus` | `#FFFFFF` | `#3A4C66` |
| `text` | `#2B3540` | `#FFFFFF` |
| `text2` | `#7A8794` | `#8C9AAB` |
| `textMuted` | `#9AA5B0` | `#4F5D6E` |
| `accent` | `#34BEED` | `#34BEED` |
| `onAccent` | `#FFFFFF` | `#0A1F2A` |
| `accentSoft` | `#E3F6FD` | `#1E3A4A` |
| `accentSoftText` | `#0B6E91` | `#7FD6F5` |
| `overlayDim` | `#000000` α 0.45 | `#000000` α 0.60 |

### 3.7 Schrift

*M PLUS Rounded 1c* (SIL Open Font License), Schnitte Regular und Medium, ins Binary eingebettet. Beim Start werden Glyphen für Latin-1 in drei Größen (12, 16, 22 logische Pixel) mit FreeType in einen Glyph-Atlas (I8-Textur) gerendert. Text wird danach nur noch als Quads aus dem Atlas gezeichnet.

### 3.8 Eingabe

Alle Geräte werden auf **Aktionen** abgebildet; UI-Code kennt nur Aktionen.

| Aktion | Wiimote | Nunchuk | GameCube-Controller |
|---|---|---|---|
| Navigieren | Steuerkreuz | Stick | Stick, Steuerkreuz |
| Zeigen | IR-Pointer (Hover = Fokus) | – | – |
| Bestätigen | A | – | A |
| Zurück | B | – | B |
| Konsole wechseln | − / + | – | L / R |
| Favorit umschalten | 1 | – | Y |
| Suche / A–Z | 2 | – | X |
| HOME-Menü | HOME | – | Start |
| Debug-Overlay ein/aus | 1 + 2 gleichzeitig | – | Z + Start |

Stick-Navigation hat Totzone und Wiederholrate mit Beschleunigung (erste Wiederholung nach 400 ms, dann alle 80 ms).

### 3.9 Audio

- **UI-Sounds:** kurze PCM-Samples (16 Bit, eigene oder CC0-lizenziert), ins Binary eingebettet, Ausgabe über ASND.
- **Musik:** `sd:/newlauncher/music/*.ogg`, zufällige Reihenfolge, Dekodierung mit Tremor in eigenem Thread, Streaming in ASND-Stimme.
- Fehlt der Musikordner, läuft der Launcher ohne Musik.

### 3.10 Video

- Videomodus aus den Systemeinstellungen (`VIDEO_GetPreferredMode`), 480p wenn eingestellt, PAL60 wenn eingestellt.
- Logisches Koordinatensystem 640×480; die Projektion skaliert auf die tatsächliche EFB-Höhe.
- 16:9 laut Systemeinstellung: logische Breite 854, horizontale Korrektur über die Projektion.
- Doppelpuffer mit VSync.

---

## 4. Architektur

### 4.1 Module

| Modul | Verantwortung | Plattform |
|---|---|---|
| `core/` | Datenmodell (Spiel, Konsole, Generation), Konsolentabelle, INI-Parser, Scanner-Logik gegen `FileSystem`-Interface, Index-Cache-Serialisierung (Big-Endian), Metadaten-Leser, N64-Auto-Wahl, VC-Mapping + Namensabgleich, Favoriten/Verlauf, Suche, No-Intro-Titelbereinigung, Texte DE/EN, Theme-Tokens, CMPR-Encoder, Emulator-Katalog, OSC-Listenparser, ZIP-Leser mit Inflate und CRC32, Installationsplan, `Result<T, Error>` | plattformfrei, host-getestet |
| `platform/` | Video-Init, Eingabe (WPAD/PAD → Aktionen), SD/USB-Mount (libfat), `FileSystem`-Implementierung, Threads (LWP), Audio (ASND, Tremor), HTTP-Client, Uhrzeit, Systemeinstellungen, NAND-Titelliste und Bannernamen | Wii |
| `gfx/` | GX-Initialisierung, Sprite-Batcher, Texturverwaltung, Cover-LRU in MEM2, Glyph-Atlas, Projektion inkl. 16:9 | Wii |
| `ui/` | Szenen (Hauptbildschirm, Details, HOME, Einstellungen, Suche, Dialog), Fokus-/Pointer-Navigation, Animationen (Easing), Debug-Overlay | Wii; Navigationslogik host-testbar |
| `launch/` | Backend-Interface, Backends `EmulatorDol`, `VcChannel`, `Nintendont`, `WiiDisc`, gemeinsamer DOL-Chainloader mit App-Booter-Stub | Wii |
| `boot/` | Portierter GPL-C-Code aus WiiFlow Lite / USB Loader GX: cIOS-Reload, Disc-Emulation (Fragment-Liste), Apploader, Patches, `NIN_CFG`-Erzeugung | Wii, nur auf Hardware testbar |
| `tools/` | Python-Skripte: `prepare_meta.py`, `prepare_covers.py`, `deploy_dolphin.py`, `dolphin_smoke.py` | PC |
| `tests/` | Host-Unit-Tests (doctest), Fixtures, Test-DOL `argv_echo` | PC / Dolphin |

**Abhängigkeitsregeln:**
- `core/` hängt von nichts Wii-Spezifischem ab (kein `#include <ogc/...>`).
- `ui/` spricht nur über Interfaces mit `platform/`, `gfx/` und `launch/`.
- `boot/` wird ausschließlich von `launch/` benutzt.

### 4.2 Fehlerbehandlung

- Keine Exceptions. Fehlbare Funktionen liefern `Result<T, Error>`; `Error` besteht aus Code (enum) und Kontext-String.
- Die UI übersetzt Fehlercodes über die Sprachtabelle in Meldungen.
- Kein Fehler in Cache, Metadaten oder Covern darf zum Absturz führen; ungültige Daten werden verworfen und neu erzeugt bzw. durch Platzhalter ersetzt.
- Ein eigener Exception-Handler zeigt bei Abstürzen einen Fehlerbildschirm (Ausnahmetyp, Register, Backtrace) mit „Zurück zum HBC“.
- Log: Ringpuffer, wird nach `sd:/newlauncher/log.txt` geschrieben und parallel per `OSReport` ausgegeben (im Dolphin-Log sichtbar).

### 4.3 Threads

| Thread | Aufgabe |
|---|---|
| Haupt | Eingabe, Update, Rendering |
| Scan | Hintergrund-Scan der Bibliothek, liefert Deltas an die UI |
| Cover | Lädt `.tex`-Dateien, konvertiert neue PNGs zu `.tex` |
| Audio | Musik-Dekodierung (Tremor) |
| Netz | Cover-Download und Emulator-Verwaltung (Liste, ZIP-Download, Entpacken), nur bei Bedarf gestartet; immer nur ein Auftrag zugleich |

Kommunikation über Message-Queues; die UI-Datenstrukturen gehören ausschließlich dem Haupt-Thread.

### 4.4 Speicher

- Cover-LRU: 16 MB in MEM2.
- Frame-Arena für temporäre Daten pro Frame; keine Heap-Allokationen im Render-Loop.
- Texturen in MEM2, 32-Byte-ausgerichtet.

---

## 5. Daten und Dateien

### 5.1 Ablage

`<lib>` ist das Bibliotheksgerät: `usb` auf der Wii, `sd` in Dolphin (Einstellung `library` in `config.ini`).

```
sd:/apps/newlauncher/            boot.dol, meta.xml (mit <ahb_access/>), icon.png
sd:/newlauncher/config.ini       Einstellungen
sd:/newlauncher/emulators.ini    Emulator-Pfade und Argument-Schemata
sd:/newlauncher/n64_vc.ini       ROM → VC-Title-ID
sd:/newlauncher/games.ini        Overrides pro Spiel
sd:/newlauncher/favorites.ini    Favoriten
sd:/newlauncher/history.ini      Verlauf (letzte 20 Starts)
sd:/newlauncher/music/*.ogg      Hintergrundmusik
sd:/newlauncher/log.txt          Log
sd:/newlauncher/emulators_installed.ini   installierte Emulator-Versionen (6.9)
sd:/newlauncher/tmp/             Download- und Entpack-Zwischenablage (wird nach jeder Installation geleert)

<lib>:/newlauncher/cache/index.bin       Spiele-Index
<lib>:/newlauncher/cache/meta.bin        Metadaten (vom PC-Skript)
<lib>:/newlauncher/cache/covers/<konsole>/*.tex   konvertierte Cover
<lib>:/newlauncher/covers_src/<konsole>/*.png   Quell-PNGs (PC-Skript oder Download)

<lib>:/wbfs/Titel [ID6]/ID6.wbfs (+ .wbf1 …) oder ID6.iso
<lib>:/games/Titel [ID6]/game.iso (+ disc2.iso)
<lib>:/roms/nes/  snes/  gb/  gbc/  gba/  n64/
```

### 5.2 Spiel-Schlüssel

- Wii/GC: ID6 (z. B. `RMCP01`).
- ROMs: `<konsole>/<dateiname>` relativ zum ROM-Ordner (z. B. `n64/Super Mario 64 (Europe).z64`).

Favoriten, Verlauf, Overrides und VC-Mapping verwenden diesen Schlüssel.

Abgeleitete Namen:
- **Cover-Datei:** `<konsole>/<ID6>` bzw. `<konsole>/<Dateiname ohne Endung>`, also z. B. `covers_src/n64/Super Mario 64 (Europe).png` und `cache/covers/n64/Super Mario 64 (Europe).tex`.
- **Metadaten-Schlüssel:** `<ID6>` bzw. `<konsole>/<Dateiname ohne Endung, kleingeschrieben>`.

### 5.3 Konsolentabelle

| Konsole | Kürzel | Generation | Ordner | Endungen |
|---|---|---|---|---|
| NES | `nes` | 3 | `roms/nes` | `.nes` |
| SNES | `snes` | 4 | `roms/snes` | `.sfc`, `.smc` |
| Game Boy | `gb` | 4 | `roms/gb` | `.gb` |
| N64 | `n64` | 5 | `roms/n64` | `.z64`, `.n64`, `.v64` |
| Game Boy Color | `gbc` | 5 | `roms/gbc` | `.gbc` |
| GameCube | `gc` | 6 | `games` | `.iso` |
| Game Boy Advance | `gba` | 6 | `roms/gba` | `.gba` |
| Wii | `wii` | 7 | `wbfs` | `.wbfs`, `.iso` |

Innerhalb einer Generation: Heimkonsole vor Handheld.

### 5.4 INI-Formate

Regeln für alle INI-Dateien:
- UTF-8, Abschnitte `[name]`.
- Schlüssel ist alles vor dem **ersten** `=`, Wert alles danach; beides wird an den Rändern getrimmt. Schlüssel dürfen Leerzeichen, Klammern und Punkte enthalten (ROM-Dateinamen).
- Kommentare nur als ganze Zeile, beginnend mit `;` oder `#`. Es gibt **keine** Kommentare am Zeilenende, weil Dateinamen `;` enthalten dürfen.
- Unbekannte Schlüssel werden ignoriert, fehlende mit Standardwerten ergänzt.

`config.ini` (Werte: `language` = `auto|de|en`, `library` = `usb|sd`, `usb_timeout` in Sekunden, `mode` = `light|dark|auto`, `region` = `auto|EN|DE|FR|US|JA`):
```ini
[general]
language = auto
library = usb
usb_timeout = 15
[theme]
mode = light
dark_from = 19:00
dark_to = 07:00
[audio]
music = 1
music_volume = 60
sounds = 1
[covers]
region = auto
[emulators]
repo = http://hbb1.oscwii.org/hbb
```

`emulators.ini` – `args` ist eine durch `|` getrennte Liste; jeder Eintrag wird ein `argv`-Element. `argv[0]` ist immer der DOL-Pfad. Platzhalter: `{dir}` (Ordner mit abschließendem `/`), `{file}` (Dateiname), `{path}` (vollständiger Pfad).
```ini
[nes]
dol = sd:/apps/fceugx/boot.dol
args = {dir}|{file}
[snes]
dol = sd:/apps/snes9xgx/boot.dol
args = {dir}|{file}
[gb]
dol = sd:/apps/vbagx/boot.dol
args = {dir}|{file}
[gbc]
dol = sd:/apps/vbagx/boot.dol
args = {dir}|{file}
[gba]
dol = sd:/apps/vbagx/boot.dol
args = {dir}|{file}
[n64_not64]
dol = sd:/apps/not64/boot.dol
args = {path}
[n64_wii64]
dol = sd:/apps/wii64/boot.dol
args = {path}
[nintendont]
dol = sd:/apps/nintendont/boot.dol
```
Die Standard-Schemata sind Ausgangswerte und werden in M3/M4 pro Emulator verifiziert (siehe Abschnitt 10).

`n64_vc.ini`:
```ini
[map]
Super Mario 64 (Europe).z64 = 000100014E414D50
```

`games.ini` (Werte: `vc|not64|wii64`):
```ini
[n64_method]
n64/Super Mario 64 (Europe).z64 = not64
```

`favorites.ini` / `history.ini`: ein Schlüssel pro Zeile, Verlauf mit Unix-Zeitstempel (`<zeit> = <schlüssel>`).

### 5.5 Binärformate

Alle Binärformate sind **Big-Endian** und werden feldweise geschrieben/gelesen (kein `fwrite` von Structs). Strings: `u16` Länge + UTF-8-Bytes.

**`index.bin`**
```
Header:  magic "NLIX" | u16 version | u16 reserviert | u32 Anzahl Ordner | u32 Anzahl Spiele
Ordner:  string pfad | u32 mtime                     (für inkrementellen Scan)
Spiel:   u8 konsole | u8 flags | string schlüssel | string titel | string pfad | u64 größe
```

**`meta.bin`** (vom PC-Skript erzeugt, nach Schlüssel sortiert → binäre Suche)
```
Header:  magic "NLMD" | u16 version | u16 reserviert | u32 Anzahl
Eintrag: string schlüssel | string titel | u16 jahr | u8 spieler | string beschreibung
```
Schlüssel laut 5.2 („Metadaten-Schlüssel“); Vergleich als Bytefolge nach UTF-8.

**`.tex`** (Cover)
```
Header:  magic "NLTX" | u16 breite | u16 höhe | u32 gx_format (14 = CMPR) | u32 datenlänge
Daten:   CMPR-Texturdaten, 32-Byte-ausgerichtet
```
Cover-Größe: 200×280 (≈ 28 KB).

Bei unbekannter Version oder fehlerhaftem Header wird die Datei verworfen und neu erzeugt.

---

## 6. Laufzeit

### 6.1 Startablauf

1. HBC startet `boot.dol` mit abgeschaltetem AHBPROT (`<ahb_access/>` in `meta.xml`).
2. SD mounten, `config.ini` laden; fehlt sie, mit Standardwerten anlegen. Ist keine SD lesbar, gelten Standardwerte.
3. Video initialisieren, Splash im Theme aus der Konfiguration (Ziel: < 0,5 s nach Start).
4. Bibliotheksgerät mounten. Bei USB: Wiederholung bis `usb_timeout`, Anzeige „Warte auf USB …“. Nach Timeout: Dialog „Erneut versuchen“ / „SD verwenden“.
5. `index.bin` und `meta.bin` laden → UI bedienbar. Fehlt der Index: erster Scan mit Fortschrittsanzeige.
6. Hintergrund-Scan: Ordner-mtime mit Index vergleichen, geänderte Ordner neu scannen, Delta an UI, Index speichern.
7. Cover-Thread lädt sichtbare Cover und zwei Reihen voraus; neue PNGs in `covers_src/` werden zu `.tex` konvertiert.

### 6.2 Scanner

- **Wii:** jede Datei `.wbfs`/`.iso` in Unterordnern von `wbfs/`; ID6 und interner Titel aus dem Disc-Header; Split-Dateien (`.wbf1` …) werden dem Hauptspiel zugeordnet.
- **GC:** `games/*/game.iso`; ID6 aus dem Header; `disc2.iso` wird dem Spiel zugeordnet.
- **ROMs:** Endungen laut Konsolentabelle, nicht rekursiv. Anzeigetitel = Dateiname ohne Endung und ohne Klammer-Tags (`(Europe)`, `[!]` …).
- Titel aus `meta.bin` haben Vorrang vor Header- und Dateinamen-Titeln.
- Sortierung: alphabetisch nach Anzeigetitel, Artikel werden nicht ignoriert.

### 6.3 Metadaten

`tools/prepare_meta.py` (PC) bekommt den Pfad der Bibliothek (USB-Platte am PC angeschlossen), liest Disc-Header und ROM-Dateinamen selbst und erzeugt Einträge nur für vorhandene Spiele:
- Wii/GC: GameTDB-Datenbank (`wiitdb.xml`) → Titel in Systemsprache, Jahr, Spieler, Beschreibung.
- Retro: libretro-Datenbanken (No-Intro-Namen) → Jahr, Spieler; Titel aus dem No-Intro-Namen.
- Schreibt `meta.bin` in `<lib>:/newlauncher/cache/`.

Fehlt `meta.bin`, funktionieren alle Funktionen; Detailansicht zeigt dann keine Beschreibung.

### 6.4 Cover

- **Wii/GC (auf der Wii):** Aktion „Fehlende Cover laden“ lädt `http://art.gametdb.com/wii/cover/<REGION>/<ID6>.png` nach `covers_src/<konsole>/<ID6>.png`. Region-Reihenfolge bei „Automatisch“: Region der Systemsprache (z. B. `DE`) → `EN` → `US` → `JA`. Fehler werden pro Cover übersprungen und am Ende zusammengefasst.
- **Retro (PC):** `tools/prepare_covers.py` bekommt den Bibliothekspfad, holt Boxarts aus libretro-thumbnails anhand des No-Intro-Namens und legt sie unter `covers_src/<konsole>/<Dateiname ohne Endung>.png` ab.
- **Konvertierung (nur auf der Wii):** PNG/JPG dekodieren (`stb_image`), in 200×280 einpassen (Seitenverhältnis erhalten, freie Ränder transparent – CMPR unterstützt 1-Bit-Alpha, so passt das Cover zu beiden Themes), CMPR kodieren, als `.tex` speichern.
- **Anzeige:** LRU-Cache 16 MB; Platzhalter-Kachel (Konsolen-Icon + Titel) bis das Cover geladen ist, dann 150 ms Einblendung.

### 6.5 Start-Backends

Gemeinsames Interface:
```cpp
struct LaunchBackend {
    virtual Result<void, Error> preflight(const Game&) = 0;   // prüft alles, ändert nichts
    virtual Result<void, Error> launch(const Game&) = 0;      // kehrt nur im Fehlerfall zurück
};
```

Ablauf beim Start: `preflight()` → bei Fehler Dialog, Launcher bleibt. Sonst: Verlauf speichern → Audio stoppen → Video herunterfahren → `launch()`.

| Backend | Konsolen | Mechanismus | Preflight prüft |
|---|---|---|---|
| `EmulatorDol` | NES, SNES, GB, GBC, GBA, N64 (Not64/Wii64) | DOL von SD laden, `argv` nach `emulators.ini` bauen, Übergabe an Chainloader | DOL existiert und ist gültig, ROM existiert |
| `VcChannel` | N64 mit VC-Mapping | `WII_LaunchTitle(titleId)` | Titel ist im NAND installiert |
| `Nintendont` | GC | Nintendont-DOL laden, `NIN_CFG`-Block (Spielpfad, Videomodus, Controller, Memcard-Emulation) als `argv[1]`; **kein** IOS-Reload vorher | DOL existiert, ISO existiert, AHBPROT ist aus |
| `WiiDisc` | Wii | `boot/`: Reload in d2x-cIOS, Fragment-Liste der Spieldatei, Apploader, Patches (Videomodus, Sprache) | cIOS vorhanden, Spieldatei lesbar |

**Chainloader:** Die Ziel-DOL wird in einen Puffer in MEM2 geladen. Ein kleiner App-Booter-Stub wird in einen freien Speicherbereich kopiert und angesprungen; er kopiert die DOL-Sektionen an ihre Zieladressen, legt die `argv`-Struktur (Magic `_arg`) an, invalidiert die Caches und springt zum Einsprungpunkt.

**`NIN_CFG`:** Struktur und Version entsprechen der installierten Nintendont-Version; Details im Plan zu M8.

### 6.6 N64-Auto-Wahl

Reine Funktion in `core/`, Eingaben: Spiel, Override, VC-Mapping, installierte Titel, vorhandene Emulator-DOLs.

1. Override aus `games.ini`, falls gesetzt und verfügbar.
2. Gemapptes VC-Inject, falls im NAND installiert.
3. Not64, falls DOL vorhanden.
4. Wii64, falls DOL vorhanden.
5. Sonst Fehler „Kein N64-Startweg verfügbar“.

Ist ein Override gesetzt, aber nicht verfügbar, wird mit Schritt 2 weitergemacht und die Detailansicht zeigt einen Hinweis. Gleiches gilt für ein gemapptes, aber nicht installiertes Inject.

### 6.7 VC-Mapping

- Dialog „VC-Injects zuordnen“ listet alle installierten Kanäle (Titeltyp `00010001`) mit Namen aus dem Banner-Header (IMET); ist der Name nicht lesbar, wird die Title-ID angezeigt.
- Für jedes N64-ROM ohne Mapping schlägt ein Namensabgleich (normalisiert, unscharf) den besten Kanal vor; der Nutzer bestätigt oder wählt manuell.
- Ergebnis wird in `n64_vc.ini` gespeichert.

### 6.8 HOME-Menü-Aktionen

- **Homebrew Channel:** Aufräumen, `exit(0)` (Rückkehr zum HBC).
- **Wii-Menü:** `SYS_ResetSystem(SYS_RETURNTOMENU, …)`.
- **Ausschalten:** `SYS_ResetSystem(SYS_POWEROFF, …)`.
- **Einstellungen:** öffnet die Einstellungsszene.

### 6.9 Emulator-Verwaltung

Ziel: Eine frische Karte mit nur dem Launcher kann alle Emulatoren selbst holen und später aktualisieren.

- **Quelle:** Open-Shop-Channel-Repository über HTTP (kein TLS auf der Wii nötig). Basis-URL aus `config.ini` `[emulators] repo`, Standard `http://hbb1.oscwii.org/hbb`; `[debug] emu_repo` ersetzt sie im Smoke-Test.
  - Liste: `<repo>/listv036.txt`. Pro App sieben Zeilen; die erste enthält Name, Zeitstempel (Unix), Icon-Größe, Entpackt-Größe, Typ, ZIP-Größe und weitere Felder, dann Anzeigename, Autor, Version, Größe, Kurz- und Langbeschreibung.
  - Paket: `<repo>/<name>/<name>.zip`, Pfade relativ zur SD-Wurzel (`apps/<name>/boot.dol` …).
- **Katalog (fest im Code):**

  | OSC-Name | `emulators.ini`-Abschnitte | DOL |
  |---|---|---|
  | `fceugx` | `nes` | `sd:/apps/fceugx/boot.dol` |
  | `snes9xgx` | `snes` | `sd:/apps/snes9xgx/boot.dol` |
  | `vbagx` | `gb`, `gbc`, `gba` | `sd:/apps/vbagx/boot.dol` |
  | `not64` | `n64_not64` | `sd:/apps/not64/boot.dol` |
  | `wii64` | `n64_wii64` | `sd:/apps/wii64/boot.dol` |
  | `Nintendont` | `nintendont` (M8) | `sd:/apps/Nintendont/boot.dol` |

- **Status je Emulator:** „nicht installiert“, „installiert“ (Version bekannt, aktuell), „Update verfügbar“ (Zeitstempel der Liste neuer als der installierte), „installiert, Version unbekannt“ (DOL vorhanden, kein Eintrag in `emulators_installed.ini`, z. B. am PC kopiert). Ein Update für „Version unbekannt“ ist erlaubt und erzeugt den Eintrag.
- **`emulators_installed.ini`:** ein Abschnitt pro OSC-Name mit `timestamp` (aus der Liste) und `version` (Text aus der Liste, darf leer sein). Wird nur nach erfolgreicher Installation geschrieben.
- **Oberfläche:** Einstellungen → „Emulatoren“ öffnet eine Liste (Name, installierte Version, verfügbare Version, Status). Aktionen: „Nach Updates suchen“ (lädt die Liste), „Installieren“/„Aktualisieren“ für die markierte Zeile, „Alle aktualisieren“. Kein Netzzugriff beim Start des Launchers; die Liste wird nur auf Aktion geladen. Während der Arbeit zeigt die Szene Fortschritt (Schritt, Bytes) und bleibt bedienbar; B bricht nach dem laufenden Schritt ab. Fehler erscheinen pro Emulator in der Liste und als Dialog.
- **Installation (Netz-Thread):**
  1. Freien Platz auf der SD prüfen: ZIP-Größe + Summe der entpackten Größen + 1 MiB Reserve, sonst Fehler „Zu wenig Platz“.
  2. ZIP nach `sd:/newlauncher/tmp/<name>.zip` streamen; Größe muss der Listenangabe entsprechen.
  3. Zentrales Verzeichnis lesen. Einträge mit absoluten Pfaden, `..`, Laufwerksbuchstaben oder anderen Methoden als 0 (stored) und 8 (deflate) machen das ganze Paket ungültig. Jeder Eintrag wird entpackt und gegen CRC32 und Größe geprüft, zuerst nach `sd:/newlauncher/tmp/<name>/`.
  4. Erst wenn alle Einträge gültig sind, wird verschoben: Dateien unter `apps/` werden ersetzt, alle anderen nur angelegt, wenn sie fehlen (Saves, Einstellungen und Speicherkarten der Emulatoren bleiben). `apps/<x>/boot.dol` wird jeweils zuletzt ersetzt, damit ein Abbruch keine halbe App mit neuer DOL hinterlässt.
  5. `emulators_installed.ini` schreiben, `tmp/` leeren, Index der Emulator-Verfügbarkeit neu laden (N64-Auto-Wahl, Preflight).
- **Inflate:** eigener kleiner Inflater in `core/` (RFC 1951), host-getestet gegen mit zlib erzeugte Fixtures; keine zusätzliche Bibliothek.
- **PC-Weg:** `tools/wii_deploy.py --emulators` lädt dieselben Pakete aus derselben Quelle und entpackt sie nach denselben Regeln auf die Karte (vor dem Download wird gefragt, welche).
- **Hinweis Nintendont:** Die OSC-Version kann älter sein als die von M8 vorausgesetzte; der M8-Preflight meldet eine zu alte Version mit Verweis auf die Aktualisierung.
- **HTTPS (sobald das Modul `wii_tls` fertig ist, Plan-Task 9):** Bis dahin nur `http://` – die Pakete sind dann nur über Größe und CRC32 geschützt, nicht gegen Manipulation unterwegs. Mit `wii_tls` gilt:
  - `parseUrl` akzeptiert `https://` (Standardport 443); `httpGet`/`httpDownload` wählen den Transport nach Schema, die Aufrufer (Cover, Emulatoren) bleiben unverändert.
  - Zertifikate werden geprüft (CA-Bündel aus `wii_tls`); ein TLS-Fehler ist ein Fehlschlag, **nie** ein stiller Rückfall auf `http://`.
  - Standard-Quelle wird die HTTPS-Adresse des Open Shop Channel; `[emulators] repo` nimmt dann `http://` und `https://`.
  - Nintendont kann zusätzlich direkt aus den offiziellen GitHub-Releases bezogen werden (nur über HTTPS erreichbar) – damit entfällt das Problem der veralteten OSC-Version.
  - Ohne `wii_tls` im Build bleibt alles wie oben (HTTP); der Build darf nicht davon abhängen.

---

## 7. Performance-Budgets (Abnahmekriterien, gemessen auf echter Wii in M7)

| Kriterium | Budget |
|---|---|
| Start bis bedienbar (Cache vorhanden, ohne USB-Anlaufzeit) | ≤ 2 s |
| Bildrate beim Scrollen und bei Animationen | konstant 60 fps (PAL50: 50 fps) |
| Sichtbares Cover aus Cache bis Anzeige | ≤ 100 ms |
| Heap-Allokationen im Render-Loop | 0 |
| Blockierung der UI durch Scan oder Download | keine |

Das Debug-Overlay (ab M1, Tastenkombination laut 3.8) zeigt CPU-Zeit pro Frame, Draw-Calls und belegten Texturspeicher.

---

## 8. Tests und Entwicklungsablauf

### 8.1 Host-Unit-Tests

- Framework: doctest; Compiler: WinLibs-GCC; Ausführung: `ctest`.
- Abgedeckt: alles in `core/` – Konsolentabelle, Scanner gegen In-Memory-Dateisystem, Index-Round-Trip gegen feste Big-Endian-Bytes, Metadaten-Leser, INI-Parser, N64-Auto-Wahl, VC-Namensabgleich, Titelbereinigung, Suche, CMPR-Encoder.
- Neue Logik in `core/` entsteht testgetrieben.

### 8.2 Dolphin-Smoke-Test (`tools/dolphin_smoke.py`)

1. Baut bzw. nimmt `newlauncher.dol` und kopiert sie mit der Fixture-Bibliothek in den SD-Sync-Ordner.
2. Startet Dolphin im Batch-Modus mit eigenem User-Ordner (`-u`) und Logausgabe in Datei.
3. Wartet im Log auf `NL_READY` (vom Launcher nach dem ersten vollständig gerenderten Hauptbildschirm ausgegeben).
4. Timeout (60 s) oder fehlende Markierung → Test fehlgeschlagen; Dolphin wird beendet.

**Chainload-Test:** Test-DOL `argv_echo` gibt alle `argv`-Elemente per `OSReport` aus. Ist in `config.ini` `[debug] autostart = <schlüssel>` gesetzt (nur in der Fixture-Konfiguration), startet der Launcher nach `NL_READY` dieses Spiel automatisch. Das Fixture-Spiel nutzt `argv_echo` als Emulator; das Skript prüft die erwarteten Argumente im Log.

### 8.3 Fixtures

- `tests/fixtures/sd/`: Bibliothek mit Dummy-Dateien (nur Disc-Header bzw. leere ROM-Dateien), `config.ini`, `emulators.ini` mit `argv_echo`.
- **Keine echten ROMs, ISOs oder WADs im Repo**; `.gitignore` schließt `*.wbfs`, `*.iso`, `*.wad`, `*.z64`, `*.n64`, `*.v64`, `*.nes`, `*.sfc`, `*.smc`, `*.gb`, `*.gbc`, `*.gba` außerhalb von `tests/fixtures/` aus.

### 8.4 Entwicklungszyklus

```
cmake --build build/wii                  # → newlauncher.dol
python tools/deploy_dolphin.py           # DOL + Testbibliothek in den SD-Sync-Ordner
Dolphin.exe -u <dev-user-dir> -e newlauncher.dol
```

- Dev-User-Ordner von Dolphin unter `../wii_tools/dolphin-user/` (SD-Sync, Logs, NAND mit Test-WADs).
- Auf der Wii (ab M7): `wiiload` über WLAN.

### 8.5 CI (GitHub Actions)

- Job „wii“: Container `devkitpro/devkitppc`, baut `newlauncher.dol` und `argv_echo.dol`, lädt sie als Artefakt hoch.
- Job „host-tests“: Ubuntu, GCC, baut und führt die Host-Tests aus.

---

## 9. Meilensteine

Jeder Meilenstein bekommt einen eigenen Implementierungsplan.

| # | Inhalt | Abnahme in |
|---|---|---|
| **M0** | Fundament: devkitPro und WinLibs installieren, Repo-Gerüst mit CMake für beide Ziele, `.gitignore`, CI, Dolphin-Dev-User-Ordner mit SD-Sync, „Hallo GX“-DOL, Smoke-Test grün | Dolphin, CI |
| **M1** | Renderer und UI-Kern: Sprite-Batcher, Glyph-Atlas, Theme hell/dunkel/auto, Eingabe-Aktionen inkl. Pointer, Szenen und Animationen, Hauptbildschirm mit Testdaten, HOME-Menü (Grundfunktionen), Debug-Overlay, Exception-Handler | Dolphin |
| **M2** | Bibliothek: SD/USB-Mount, Scanner, `index.bin`, Favoriten, Verlauf, A–Z-Sprung, Suche mit Bildschirmtastatur, Detailansicht | Host + Dolphin |
| **M3** | Emulator-Start: Chainloader + App-Booter, `argv_echo`-Test, `emulators.ini`, NES/SNES/GB/GBC/GBA, Preflight-Dialoge | Dolphin |
| **M4** | N64: Not64/Wii64 über `EmulatorDol`, `VcChannel`, NAND-Titelliste mit Bannernamen, VC-Zuordnungsdialog, Auto-Wahl | Dolphin |
| **M5** | Cover und Metadaten: `prepare_meta.py`, `prepare_covers.py`, HTTP-Download, PNG→CMPR, Cover-LRU | Host + Dolphin |
| **M6** | Feinschliff: UI-Sounds, Musik, vollständiges HOME-Menü, Einstellungen, DE/EN | Dolphin |
| **M7** | Hardware-Abnahme: M0–M6 auf echter Wii mit USB-Platte, Performance-Budgets messen | Wii |
| **M8** | Nintendont-Backend | Wii |
| **M9** | Wii nativ: `boot/`-Port (cIOS, Disc-Emulation, Apploader, Patches) | Wii |
| **M10** | Emulator-Verwaltung (6.9): Katalog, Listenparser, ZIP/Inflate, Installation auf der Wii, Einstellungsszene, `wii_deploy.py --emulators`. Wird vor M8 umgesetzt, weil vollständig in Dolphin testbar | Host + Dolphin |

---

## 10. Risiken und frühe Verifikationen

| Risiko | Prüfung in | Rückfallebene |
|---|---|---|
| Emulatoren akzeptieren das vorgesehene `argv`-Schema nicht | M3 (GX-Emulatoren), M4 (Not64, Wii64) | Schema in `emulators.ini` anpassen; sonst Emulator ohne ROM starten |
| Bannernamen aus dem NAND unter HBC-Rechten nicht lesbar | M4 | Anzeige der Title-ID, Name manuell in `n64_vc.ini` als Kommentar |
| Dolphin-SD-Sync oder Chainload verhält sich anders als erwartet | M0 / M3 | SD-Image statt Sync-Ordner; Chainload-Test nur auf Hardware |
| Genaue Dolphin-Logger-Einstellungen für `OSReport`-Ausgabe in Datei | M0 | Ausgabe stattdessen in Datei auf der virtuellen SD, Skript liest diese |
| GameTDB-Cover nur per HTTPS erreichbar | M5 | Cover-Download zusätzlich in `prepare_covers.py` für Wii/GC |
| Performance-Budgets in Dolphin nicht messbar | M7 | Debug-Overlay ab M1, Messung auf Hardware |
| Open-Shop-Channel-Endpunkt per HTTP fällt weg oder ändert das Listenformat | M10 | URL in `config.ini` umstellbar; Emulatoren weiter per PC-Skript oder von Hand kopieren; TLS später nachrüstbar |
| `NIN_CFG`-Version passt nicht zur installierten Nintendont-Version | M8 | Version beim Preflight aus Nintendont-Binary lesen, unterstützte Versionen festlegen |
