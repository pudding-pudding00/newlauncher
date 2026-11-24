# Projektstand (Übergabe)

Stand: 2026-11-24, `main` nach PR #28 (M19) und Review-Bereinigung. Regeln für Agenten stehen in [AGENTS.md](../AGENTS.md).

## Erledigt

| Meilenstein | Inhalt | PR |
|---|---|---|
| M0–M10 | v1: Launcher, Bibliothek, Booter, Emulatoren, Cover, Leistung (Spec `2026-11-09-wii-launcher-design.md`) | bis #13; v2-Spec #14 |
| M11 | Lizenz und TLS (wii_tls vendort, HTTPS-Standardquellen) | #15 |
| M12–M17 | Bibliothek universell, System-Erkennung, Spieloptionen, Cheats, Dateisysteme, Komfort | bis #23 |
| M18 | Banner: GC statisch, Kanal-Banner mit Animation und Ton | #24, Kleinigkeiten #25 |
| – | wii_tls auf Release **v0.1.0** (`9b1689b`); errno-Fix ist upstream | #26 |
| – | Übergabe-Dokumentation (`AGENTS.md`, `STATUS.md`, `CLAUDE.md`) | #27 |
| M19 | Weitere Startwege: optische Discs (Wii & GC über Nintendont), EmuNAND & Kanäle, vWii-Feinschliff | #28 |

Die CI (`.github/workflows/build.yml`) baut Wii mit und ohne TLS und führt die Host-Tests aus.

## Offen

Alle Code-Meilensteine (M0–M19) der v2-Spec sowie die offenen Review-Kleinigkeiten sind umgesetzt; offen ist die Hardware-Abnahme auf echter Hardware.

### 1. Hardware-Abnahme auf der echten Wii

Bisher ist **kein** Punkt abgehakt, die Spalte „Ergebnis“ ist überall leer bzw. `ungeprüft` (gesamt 125 Punkte).

| Liste | Punkte |
|---|---|
| [hardware-acceptance.md](hardware-acceptance.md) (v1/M7, mit Leistungsbudgets) | 65 |
| [acceptance/m11-wii.md](acceptance/m11-wii.md) bis [m19-wii.md](acceptance/m19-wii.md) | 8 + 7 + 6 + 7 + 6 + 6 + 7 + 6 + 7 = 60 |

- **Pflicht-Aufbauten** (Spec v2, Abschnitt 13):
  - Wii mit USB-Platte in FAT32;
  - Wii ohne SD-Karte;
  - zweite USB-Partition, wenn die Platte das zulässt.
- **Optional:** vWii, Wii mini, NTFS, ext4, WBFS.
- Was ungeprüft bleibt, kommt als „ungeprüft“ in die Release-Notes.
- **Test-Wii:**
  - System-Menü **4.3E** (PAL), MAC-Adresse **00-00-00-00-00-00**.
  - Homebrew Channel ist installiert. IP im LAN zuletzt **192.0.2.10**, wiiload auf TCP 4299.
- **Letzter Versuch (2026-11-24):** `python tools/wii_deploy.py --wiiload 192.0.2.10` brach mit „transfer failed“ ab, und die Wii war danach nicht mehr erreichbar.
  - Vermutlich war der HBC nicht im Vordergrund oder das WLAN schwach. Schon vorher gab es 50 % Paketverlust beim Ping.
  - Es lief nichts auf der Wii, und auf der SD-Karte wurde nichts geändert.
- **Nächster Schritt:** HBC öffnen, die IP unten rechts prüfen und den Versand wiederholen.
  - Für die echte Abnahme braucht der Nutzer am Controller eine vorbereitete SD-Karte. Wie das geht, steht unter „Vorbereitung“ in `acceptance/m11-wii.md`; vor jedem Schreiben auf die Karte fragen.
  - Die Hardware-Suiten von wii_tls sind laut Release v0.1.0 schon auf einer Wii gelaufen. Wiederholen auf dieser Wii: siehe `acceptance/m11-wii.md`.

### 2. Bewusste Eigenheiten / verbleibende Hinweise (M17)

- **Kindersicherung (`[debug] pin_input`):** Hält die Test-PIN für automatisierte Dolphin-Smoke-Tests in `config.ini` (für den normalen Nutzerbetrieb auf der Wii nicht relevant).
- **Benchmark:** Der 5000er-Benchmark läuft bewusst nur im Release-Build.

### 3. Bekannte Grenzen der Banner (M18), mögliche spätere Ausbaustufen

- Text- und Fensterflächen (`txt1`/`wnd1`) werden nicht gezeichnet.
- Texturen werden immer mit `GX_CLAMP` gezeichnet.
- Gedreht wird nur um die z-Achse.
- `.wbf1`-Dateien und Spiele auf WBFS-Partitionen zeigen nur das Cover.

## Umgebung (Windows-Rechner des Nutzers)

- Repo: `<repo-root>`.
- Daneben liegen `..\wii_tools\Dolphin-x64` und `..\wii_tools\dolphin-user`, der Dev-User für Dolphin.
- devkitPro unter `C:\devkitPro`, wiiload unter `C:\devkitPro\tools\bin\wiiload.exe`.
- Host-Tests unter Windows brauchen den WinLibs-GCC und Ninja (siehe README). Die CI führt sie unter Linux aus.
- Ein alter `build/wii`-Cache ist unproblematisch: Das CMake setzt die wii_tls-Krypto-Einstellungen per FORCE.
