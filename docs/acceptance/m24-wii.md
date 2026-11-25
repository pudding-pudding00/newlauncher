# Hardware-Abnahme M24: Raster, Sortierung, Cover-Speicher (nur Wii / vWii)

Status: **ungeprüft** (echte Wii mit HBC, newlauncher (TLS-Build), WLAN, SD-Karte, USB-Laufwerk mit Wii- und Retro-Spielen).
Spec: `docs/superpowers/specs/2026-11-25-m24-raster-cover-speicher-design.md`.

## Prüfpunkte

| Punkt | Prüfung | Erwartetes Ergebnis | Ergebnis |
|---|---|---|---|
| R1 | Hauptbildschirm, Wii-Konsole | 6 Spalten größerer Cover (128 × 179 bei 16:9), kaum Rand, Infoleiste schmaler; nichts abgeschnitten | ungeprüft |
| R2 | Zuletzt und Favoriten mit gemischten Konsolen (Wii, SNES, GBA) | Jede Kachel im Format ihrer Konsole; Reihe so hoch wie die höchste Kachel, kleinere vertikal zentriert; Fokus und Zeiger treffen die richtige Kachel | ungeprüft |
| R3 | Suche mit Treffern verschiedener Konsolen | wie R2 | ungeprüft |
| R4 | Konsole mit geladenen Covern | Keine Konsolen-Plakette auf Kacheln mit Cover; Kacheln ohne Cover und Discs zeigen sie weiter | ungeprüft |
| R5 | Fokus auf eine Kachel der obersten bzw. untersten Reihe | Die vergrößerte Kachel liegt über Konsolen- und Infoleiste und über den Nachbar-Kacheln, nicht darunter; beim Rand-Scrollen mit dem Zeiger schwebt keine weggescrollte Kachel über der Leiste | ungeprüft |
| S1 | Erster Start nach dem Update ohne `[library] sort` | Spiele alphabetisch nach Titel | ungeprüft |
| S2 | A–Z-Leiste öffnen (2 auf der Wiimote, X am GC-/Classic-Controller), dann − drücken | Toast „Sortiert nach Jahr“, Liste nach Jahr; nochmal − → „Sortiert nach Titel“; Leiste bleibt offen | ungeprüft |
| S3 | Nach S2 neu starten | Zuletzt gewählte Sortierung bleibt (`config.ini`) | ungeprüft |
| D1 | „Fehlende Cover laden“ mit Spielen aller Konsolen | Reihenfolge im Log (`NL_COVER_DL`): Wii, GC, GBA, N64, GBC, SNES, GB, NES | ungeprüft |
| D2 | Nach dem Update „Fehlende Cover laden“ (oder Autostart) mit der vorhandenen Bibliothek | Die bisher fehlenden N64-/GB-/GBC-/SNES-/NES-Cover (z. B. Banjo-Kazooie (USA) (Rev 1), Super Mario Bros. Deluxe (USA, Europe) (Rev 2), Gyromite ~ Gyro (World)) erscheinen; alte `missing.txt` wird ignoriert | ungeprüft |
| H1 | Einstellungen → „Cover-Cache löschen“, A kurz drücken | Wert „A halten zum Löschen …“, Balken läuft an; nach Loslassen nichts gelöscht | ungeprüft |
| H2 | A 1 s halten | Toast „Cover werden gelöscht … n“, Oberfläche bleibt bedienbar; Ende „Cover-Cache gelöscht (n Dateien)“, Log `NL_COVERS_CLEARED files=<n> failed=0 ms=<t>` | ungeprüft |
| H3 | Während „Fehlende Cover laden“ läuft: H2 | Kein Hänger; Download bricht ab, ohne Zusammenfassungs-Dialog; danach löschen wie H2 | ungeprüft |
| H4 | A halten und dabei nach unten wechseln bzw. B | Abbruch, nichts gelöscht | ungeprüft |
| C1 | Frische Installation, Spiele auf USB | Cover landen unter `sd:/newlauncher/covers_src/…` (Einstellung „Cover-Speicher: SD-Karte“) | ungeprüft |
| C2 | Einstellung auf „Spielgerät“ umstellen, speichern, „Fehlende Cover laden“ | Log `NL_COVER_STORAGE device`; neue Cover auf dem USB-Laufwerk; vorhandene SD-Cover werden nicht mehr angezeigt (keine Migration) | ungeprüft |
| C3 | SD schreibgeschützt (Schieber), Speicher „SD-Karte“ | `NL_WARN`, Cover gehen auf das Spielgerät | ungeprüft |

## Dolphin (2026-11-25, grün)

- `--cover-server --tls --debug clear_covers=1`: `NL_COVER_DL_DONE ok=3 missing=6 failed=0`, dann
  `NL_COVERS_CLEARED files=9 failed=0 ms=16`.
- `--cover-server --tls --debug clear_covers=2` (Löschen gleich nach dem Download-Start): der wartende Auftrag wird
  verworfen, `NL_COVERS_CLEARED files=4 failed=0 ms=96`.
- `--cover-server --tls` mit libretro-Symlink-Text und abweichendem Namen: `NL_COVER_DL n64/Super Mario 64 (Europe)
  (En,Fr,De) libretro ok` (über den Symlink), `snes/F-Zero (Europe) libretro ok` (über „(En)“),
  `NL_COVER_DL_DONE ok=5 missing=4 failed=0`.
- Kandidaten-Sucher gegen die libretro-thumbnails-Bäume: 698/698 Retro-Spiele der Test-Bibliothek gefunden, im
  Schnitt 1,02 Anfragen je Spiel.
- `--tls-wrong-ca`, `--remote-check`, `--return-stub`, `--fresh-data`, No-TLS-Build mit `--cover-server`: OK.
