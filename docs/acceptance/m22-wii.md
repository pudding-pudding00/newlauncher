# Hardware-Abnahme M22: Retro-Cover aus dem Netz (nur Wii / vWii)

Status: **ungeprüft** (erfordert eine echte Wii mit HBC, newlauncher (TLS-Build) auf der SD-Karte, WLAN und einige
Retro-ROMs mit No-Intro-Dateinamen). Spec: `docs/superpowers/specs/2026-11-25-m22-retro-cover-design.md`.

## Vorbereitung

- ROMs unter `<wurzel>/roms/<konsole>/`, z. B. `nes/Super Mario Bros. (World).nes`,
  `snes/Super Metroid (Japan, USA) (En,Ja).sfc`, und eine Datei mit Fantasienamen (z. B. `nes/Testspiel.nes`).
- Für diese ROMs weder `covers_src/<konsole>/<stamm>.png` noch `cache/covers/<konsole>/<stamm>.tex`.
- `[covers] auto = 1` (Standard), `newlauncher/cache/covers/missing.txt` gelöscht.
- Log: `sd:/newlauncher/log.txt` bzw. `python tools/wii_remote.py --host 192.0.2.10 log`.

## Prüfpunkte

| Punkt | Prüfung | Erwartetes Ergebnis | Ergebnis |
|---|---|---|---|
| R1 | newlauncher starten, keine Aktion | Log `NL_COVER_AUTO jobs=<n> skipped=0`, dann `NL_COVER_DL nes/Super Mario Bros. (World) libretro ok`; die Retro-Cover erscheinen nach und nach in der Bibliothek | ungeprüft |
| R2 | Nach R1 | `Testspiel` meldet `missing`; Log `NL_COVER_MISSING_LIST sd: entries=<k>`; `missing.txt` enthält `nes/Testspiel` | ungeprüft |
| R3 | newlauncher neu starten | `NL_COVER_AUTO jobs=0 skipped=<k>`: keine erneuten Anfragen für Fehltreffer, keine für schon geladene Cover | ungeprüft |
| R4 | Einstellungen → „Fehlende Cover laden“ | Fehltreffer werden erneut versucht (`NL_COVER_DL nes/Testspiel missing`); Spiele mit Cover bleiben unberührt | ungeprüft |
| R5 | ROM auf USB (Bibliothek USB), keine Cover | Cover landen unter `usb:/newlauncher/covers_src/…`, Fehlliste unter `usb:/newlauncher/cache/covers/missing.txt` | ungeprüft |
| R6 | Build ohne TLS (`--no-tls`) starten | Keine Retro-Aufträge (`jobs` zählt nur Wii/GC), kein Versuch über HTTP | ungeprüft |

## Bekannte Grenzen

- Der Dateiname der ROM muss dem libretro-/No-Intro-Namen entsprechen (Region und Revision eingeschlossen),
  sonst gibt es keinen Treffer.
- Viele Aufträge dauern auf der Wii: libretro-PNGs sind 300–550 KB groß (etwa 1–1,5 s je Cover bei ~400 KB/s);
  die Verbindung wird wiederverwendet (gemessen in M23).
