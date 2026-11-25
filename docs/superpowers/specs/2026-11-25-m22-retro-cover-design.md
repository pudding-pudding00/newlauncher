# M22: Retro-Cover aus dem Netz (Kurz-Spec)

## Absicht

Wunsch des Nutzers: „emulator roms auch cover“, „der launcher [soll] die cover aus dem netz ziehen und cachen“.
Bisher lädt der Launcher nur Wii/GC-Cover (GameTDB, seit M21 auch automatisch). Retro-Cover (NES, SNES, GB, GBC,
N64, GBA) kamen nur über das PC-Skript `tools/prepare_covers.py`, und dessen Quelle `thumbnails.libretro.com`
liefert seit kurzem für alles 404 (geprüft 2026-11-25).

## Ziele

- Der Launcher lädt fehlende Retro-Boxarts selbst, im selben Lauf wie Wii/GC: automatisch nach dem Start
  (`[covers] auto`) und über „Fehlende Cover laden“.
- Cache wie bei Wii/GC: PNG nach `<wurzel>/newlauncher/covers_src/<konsole>/<stamm>.png`, die Textur danach
  nach `cache/covers/…tex`. Vorhandene Cover werden nie neu geladen.
- Fehlschläge (404) merkt sich der Launcher in `<wurzel>/newlauncher/cache/covers/missing.txt`. Der automatische
  Lauf überspringt sie; der Lauf von Hand probiert alle erneut und schreibt die Liste neu.
- `tools/prepare_covers.py` nutzt dieselbe neue Quelle.

## Nicht-Ziele

- Keine Zuordnung über CRC oder Datenbank: Treffer nur, wenn der Dateiname dem No-Intro-Namen entspricht
  (die Sammlung des Nutzers ist durchgehend so benannt, 698 ROMs).
- Keine Snaps/Titelbilder, nur Boxarts.
- Kein paralleles Laden.

## Quelle

`https://raw.githubusercontent.com/libretro-thumbnails/<repo>/master/Named_Boxarts/<name>.png`

- `<repo>`: libretro-Systemname mit `_` statt Leerzeichen, z. B. `Nintendo_-_Nintendo_64`.
- `<name>`: Dateiname ohne Endung, `&*/:\`<>?\|"` durch `_` ersetzt (libretro-Regel), dann prozentkodiert
  (alles außer `A-Z a-z 0-9 - . _ ~`).
- Geprüft 2026-11-25: 200 image/png, 120–370 KB, höchstens 512 px breit.
- Nur HTTPS (GitHub kennt kein HTTP). Im Build ohne TLS gibt es daher keine Retro-Cover; kein Rückfall auf HTTP.
- `[covers] retro_base` ersetzt die Basis bis vor `/<repo>` (Standard wie oben, ohne TLS leer = aus).
  `[debug] cover_url` ersetzt für Smokes beide Quellen.

## Was dem Bauenden bleibt

Log-Marker, Aufteilung der Funktionen, Testfälle.
