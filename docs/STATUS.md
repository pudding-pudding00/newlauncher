# Projektstand (Übergabe)

Stand: 2026-11-26, M28 (OneDrive als zweiter Anbieter, Anbieterwechsel mit Abgleich aller Spiele, „Alle abgleichen“, Abmelden mit Widerruf) umgesetzt; M27 (Cloud-Spielstände mit Google Drive: Abgleich vor dem Start, Upload nach dem Spielen, Konfliktdialog) umgesetzt; M26 (lokale Spielstände: Sichern, Zurückspielen, auch in den NAND-Save-Ordner) umgesetzt; M25 (WLAN robuster und mit Info-Fenster) umgesetzt; M24 (größeres Raster, Sortierung, Cover-Speicher auf der SD, Löschen ohne Hänger) umgesetzt, Hardware-Abnahme von M21 bis M28 offen. Regeln für Agenten stehen in [AGENTS.md](../AGENTS.md).

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
| – | Emulator-Verwaltung „Alle installieren“, WLAN-Zustand in der Infoleiste | #30, #31 |
| M20 | Direktstart von ROMs, Rückkehr-Stub, Fernsteuerung (TCP 4300), Selbst-Update, wii_tls **v0.2.0** mit HTTP/1.1-Client | #33 |
| M21 | „Wii-Menü“ im Wii-Spiel führt über den Rückkehr-Stub zurück in newlauncher; Cover werden nach dem Start automatisch geladen (`[covers] auto`) | #34 |
| M22 | Retro-Cover (NES bis GBA) lädt der Launcher selbst von libretro-thumbnails (GitHub, nur HTTPS) und merkt sich Fehltreffer in `cache/covers/missing.txt` | #36 |
| M23 | Einstellung „Cover-Cache löschen“, Raster scrollt mit dem Zeiger an den Rändern, Kachelhöhe nach Cover-Format der Konsole, Download-Messung (`[debug] perf`) | #37 |
| – | wii_tls auf Release **v0.3.0** (`b648ac2`): P-256-Signaturprüfung beschleunigt (Handshake laut Upstream 66 → 28 ms auf der Wii), Sende-Fix für IOS, mTLS und Streaming-Uploads (ungenutzt) | #38 |
| – | wii_tls auf Release **v1.0.0** (`77229ff`, erste stabile Version): Schreib-Obergrenze gegen hängende Gegenstellen, DNS-Fallback über bis zu 4 Adressen, Netz-Reset bei IP-Wechsel, Sicherheits-Fixes | #39 |
| M24 | Cover-Downloads nach Konsolengeneration (neu → alt), Sortierung Titel/Jahr über − in der A–Z-Leiste, größeres Raster mit gemischten Kachelformaten in Zuletzt/Favoriten/Suche, Plakette nur ohne Cover, „Cover-Cache löschen“ durch Halten von A und im Hintergrund (wii_tls-Abbruch-Callback), Cover-Speicher SD-Karte/Spielgerät, Retro-Cover-Sucher (libretro-Symlinks, Namensvarianten) | #40 |
| M25 | WLAN: Backoff mit `net_deinit` zwischen Versuchen, Router-Check (Zustand „eingeschränkt“), Hintergrund-Check 5–30 s, genaueres Symbol (Lauflicht, gelber/roter Punkt), Info-Fenster per Klick aufs Symbol mit „Neu verbinden“, Hinweise bei Zustandswechsel | PR #43 |
| M26 | Lokale Spielstände (Teil 1 der Cloud-Spielstände): Details-Zeile „Spielstand …“ mit eigener Szene, Sichern als ZIP unter `newlauncher/saves/<einheit>/` (höchstens 5), Zurückspielen mit Rückfrage, Journal mit Rollback beim Start; Wii-NAND über ISFS-Rechte-Patch (nur `/title/…/data`), EmuNAND, Nintendont, Emulatoren mit `save_dir` | PR #45 |
| M27 | Cloud-Spielstände mit Google Drive (Teil 2): Anmelden per Gerätecode, Abgleich vor dem Spielstart (Pull, Konfliktdialog, B = ohne Abgleich), Hintergrund-Upload nach dem Spielen über `pending.ini`, Tokens verschlüsselt auf der SD, Drive-Fake und Smokes `--cloud-fake drive --cloud-step …` | PR #46 |
| M28 | Cloud-Spielstände Teil 3: OneDrive (Graph, App-Ordner, Gerätecode über `consumers`), Anbieterwahl, Wechsel mit Abgleich aller Spiele und einmaligem Konfliktdialog, „Alle abgleichen“ (abbrechbar), Abmelden mit Google-Widerruf, Graph-Fake und Smokes `--cloud-fake drive\|onedrive` | PR folgt |

Die CI (`.github/workflows/build.yml`) baut Wii mit und ohne TLS und führt die Host-Tests aus.

## Offen

### 0. Nach M20

Spec `docs/superpowers/specs/2026-11-25-m20-direktstart-design.md`, Abnahme
[acceptance/m20-wii.md](acceptance/m20-wii.md): R1–R6, F1, U2 auf der Wii **bestanden**.

- **U1 ungeprüft:** Update aus den GitHub-Releases braucht ein Release von `pudding-pudding00/newlauncher`.
  Bisher gibt es keins (`NL_SELF_UPDATE fail manifest`). Gegen den Dolphin-Smoke `--update-server --tls` ist es grün.
- **R6, Einschränkung USB-Port:** Das EHCI-Modul von d2x (cIOS 249) liest nur USB-Port 0. Steckt die Platte am
  anderen Port, liefert `WDVD_SetFragList` `-4`. Der Booter zeigt dann eine Fehlermeldung mit Hinweis auf den
  Port (DE+EN), statt schwarz zu bleiben. Der Launcher (IOS 58) liest beide Ports. Details in `boot/UPSTREAM.md`.
- **Offenes Rätsel (nicht mehr relevant für R6):** Mit einer Wiederholungsschleife um `Disc_SetUSB`
  (24 × 250 ms) bzw. einer `/dev/usb2`-Abfrage stürzte der Booter vor der Fehleranzeige mit „Program“-Exception im
  Bereich `0x80AB00xx` ab (in `CON_Init`). Der Code dort ist im ELF gültig, also wurde der Speicher zur Laufzeit
  überschrieben. Beides ist wieder entfernt. Wer im Booter künftig länger wartet oder `/dev/usb2` anspricht, muss
  das zuerst klären.
- **Nebenbefund M13:** cIOS-Slots 248–251 erscheinen in der Systeminfo als `other` statt `d2x`.
  `src/platform/wii_disc.cpp` darf die `.app`-Datei unter Stock-IOS 58 ohne ISFS-Patches nicht lesen.
  Slot 249 ist nachweislich d2x (`BlockIOSReload` gelingt).
- **Upstream `pudding-pudding00/wii_tls`:** `z_errmsg` kollidiert trotz `Z_PREFIX` (zlib 1.3.1). Hier per Define
  `z_errmsg=wtls_z_errmsg` umgangen, upstream melden.
- **M21 (2026-11-25):** „Wii-Menü“ im Wii-Spiel führt zurück in newlauncher: Der Booter biegt `ES_LaunchTitle` in
  `__OSLaunchMenu` auf den Rückkehr-Stub bei `0x80001800` um, newlauncher startet mit `--from-game`, hebt die
  d2x-Reload-Sperre auf und lädt IOS 58. Cover lädt newlauncher nach dem Scan bei verbundenem WLAN selbst
  (`[covers] auto = 1`). Abnahme: [acceptance/m21-wii.md](acceptance/m21-wii.md), alles **ungeprüft**. Offene
  Prüfpunkte: d2x-Entsperrung (Modus 0), AHBPROT nach dem Reload (GC-Start, G5), ob Spiele `0x80001800–0x80003000`
  unberührt lassen. Grenzen: nicht mit Cheats, nicht für NAND-Kanäle (Start ohne Booter).
- **M22 (2026-11-25):** „Fehlende Cover laden“ (manuell und automatisch) holt auch Retro-Boxarts von
  `raw.githubusercontent.com/libretro-thumbnails/<System>/master/Named_Boxarts/<Name>.png` (der ROM-Dateiname
  muss dem No-Intro-Namen entsprechen). Nicht gefundene Stämme landen in `<wurzel>/newlauncher/cache/covers/missing.txt`;
  der automatische Lauf überspringt sie, der manuelle versucht alle erneut. Ohne TLS-Build keine Retro-Cover.
  `[covers] retro_base` ersetzt die Quelle, `[debug] cover_url` ersetzt im Smoke beide. Abnahme:
  [acceptance/m22-wii.md](acceptance/m22-wii.md), **ungeprüft**.
- **M23 (2026-11-25):** Einstellungen → „Cover-Cache löschen“ (zweimal A) löscht `covers_src`, `cache/covers` und
  `missing.txt` auf allen Cover-Wurzeln. Der Wiimote-Zeiger scrollt das Raster in 40-px-Randstreifen. Konsolenlisten
  haben Kacheln im Cover-Format (SNES/N64 quer 10:7, GB/GBC/GBA 1:1, sonst 5:7); Cover werden unverzerrt eingepasst.
  Messung auf der Wii: Keep-Alive greift schon (ein Handshake, 122 ms), die Zeit geht in die Übertragung
  (300–550 KB je libretro-PNG bei ~400 KB/s) und ~170 ms Speichern. Parallele Downloads bräuchten einen
  wii_tls-Umbau, offen. Abnahme: [acceptance/m23-wii.md](acceptance/m23-wii.md), **ungeprüft**.
- **M24 (2026-11-25):** Raster 128 × 179 (16:9), Infoleiste 60 px, Konsolenleiste 48 px. Gemischte Listen: Reihe so
  hoch wie die höchste Kachel. Sortierung Standard Titel, − in der A–Z-Leiste schaltet Titel ↔ Jahr. Cover liegen
  standardmäßig auf der SD (`[covers] storage = sd|device`); vorhandene Cover werden beim Umstellen nicht verschoben.
  Löschen: A 1 s halten; ein eigener Thread löscht, der Download bricht über `abort_cb` sofort ab.
  Retro-Cover-Sucher: libretro-thumbnails nutzt Git-Symlinks (raw.githubusercontent liefert nur den Zielnamen als
  Text) und weicht oft im Namen ab; der Launcher folgt Symlinks (2 Sprünge) und probiert bis zu 16 Namensvarianten
  (`libretroNameCandidates`). Die Fehlliste hat eine Versionszeile, ältere Listen werden verworfen.
  Abnahme: [acceptance/m24-wii.md](acceptance/m24-wii.md), **ungeprüft**.
- **M27 (2026-11-26):** Cloud-Kern + Google Drive (Spec `docs/superpowers/specs/2026-11-26-cloud-spielstaende-design.md`). Einstellungen → Cloud-Spielstände: Anbieter, Anmelden per Gerätecode (Adresse + Code am Bildschirm), Abmelden. Vor dem Start gleicht ein Tor den Spielstand ab (`decideSync`: Pull mit Sicherung vorher, Konfliktdialog mit Gerätename/Serverzeit, offline sofort weiter); nach dem Spielen wandert die Sitzung beim nächsten Start nach `cloud/pending.ini` und wird im Hintergrund hochgeladen. Drive: Ordner „newlauncher“, Scope `drive.file`, multipart bis 5 MB, darüber resumable, Refresh bei 401; Tokens bleiben je Sitzung im Speicher. Client-ID und Secret nur in `config.ini [cloud]` (Anleitung [cloud-setup.md](cloud-setup.md)). Ohne TLS `NL_CLOUD off notls`. Smokes `--cloud-fake drive --cloud-step link|push|pull|conflict|refresh|offline` gegen `tools/cloud_fake.py`. Abnahme: [acceptance/m27-wii.md](acceptance/m27-wii.md), **ungeprüft** (braucht eigene Google-App-Registrierung). Weiter mit M28 (OneDrive + Anbieterwechsel).
- **M28 (2026-11-26):** OneDrive + Anbieterwechsel (Plan `docs/superpowers/plans/2026-11-26-m28-onedrive.md`). OneDrive über Microsoft Graph im App-Ordner (`Files.ReadWrite.AppFolder`), Rev = cTag, Upload mit `If-Match` (412 → neu entscheiden), Meta in der Datei-Beschreibung, Downloads folgen der 302-Umleitung ohne `Authorization`. Einstellungen → Cloud-Spielstände: Anbieter umschalten (ohne Client-ID übersprungen), nach jeder Anmeldung Abgleich aller Spiele (`core/cloud_sync_all`; bei Konflikten ein Dialog für alle: Wii / Cloud / beim Start fragen), Zeile „Alle abgleichen“ mit Fortschritt und Abbruch, Abmelden widerruft bei Google das Token (OneDrive: nur lokal). Smokes `--cloud-fake drive|onedrive --cloud-step link|push|pull|conflict|refresh|offline|syncall|switch|unlink`. Abnahme: [acceptance/m28-wii.md](acceptance/m28-wii.md), **ungeprüft** (braucht eigene Entra-App). Weiter mit M29 (Dropbox, Kopplungsseite, QR, Tastatur).
- **M26 (2026-11-26):** Erster Schritt der Cloud-Spielstände (Spec `docs/superpowers/specs/2026-11-26-cloud-spielstaende-design.md`, Ziele 1–4), noch ohne Cloud. Spielstand-Szene aus den Details: Sichern, Zurückspielen (vorher automatische Sicherung, Journal `saves/restore.ini`, Rollback beim nächsten Start), Sicherung löschen. Echter NAND nur über den ISFS-Rechte-Patch und nur im Save-Ordner; ohne AHBPROT „NAND gesperrt“. Spike S3: Dolphin schreibt den Save-Ordner; IOS benennt nur mit gleichem Dateinamen um (Zwischendatei `/tmp/<name>`). Smokes `--save-fixture gb|nand|emunand --save-step backup|restore|recover`. Abnahme: [acceptance/m26-wii.md](acceptance/m26-wii.md), **ungeprüft** (vor dem ersten echten NAND-Schreiben fragen, BootMii-Backup).
- **Smoke `--remote-check` wackelt (2026-11-25):** 2 von 11 Läufen meldeten „REMOTE KEY (Fokus blieb …)“,
  unabhängig von wii_tls (die Fernsteuerung nutzt es nicht). Vermutlich setzt ein Zeigerereignis in Dolphin den
  Fokus zurück auf die Kachel unter dem Zeiger. Offen.

**Hinweise für die Arbeit:**

- Wii: 192.0.2.10, HBC/wiiload auf Port 4299, Fernsteuerung auf Port 4300
  (`python tools/wii_remote.py --host 192.0.2.10 state|start|launch <ID6>|…`). Vor jedem Schreiben auf die SD
  den Nutzer fragen.
- **Niemals** rohe TCP-Verbindungen auf Port 4299 (z. B. `Test-NetConnection`): Das blockiert wiiload.
- Der MEM2-Absturzbericht (`NLCR` bei `0x932F0000`, im Log `NL_PREV_CRASH`) überlebt keinen wiiload-Upload.
- Dolphin emuliert kein USB-Laufwerk: USB-Fehler zeigen sich nur auf der Hardware.
- Booter-Disassemblierung: `powerpc-eabi-objdump -d build/wii/game_booter.elf`. Der Booter liegt ab `0x80A80000`.
- Dateien mit dem Edit-Tool bearbeiten, nicht mit PowerShell `Get-Content`/`Set-Content`. PS 5.1 liest UTF-8
  ohne BOM als ANSI und erzeugt Mojibake.

### 1. Hardware-Abnahme auf der echten Wii

Abgehakt ist bisher nur M20 (bis auf U1). In den übrigen Listen ist die Spalte „Ergebnis“ überall leer bzw. `ungeprüft` (125 Punkte).

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
  - Homebrew Channel ist installiert, IP im LAN **192.0.2.10**, wiiload auf TCP 4299. AHBPROT aus,
    cIOS 249 = d2x. USB-Platte (FAT32, 808 Spiele) am USB-Port 0.
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
- Host-Tests unter Windows brauchen den WinLibs-GCC und Ninja (siehe README) sowie Kitware-CMake
  (WinGet-Paket `Kitware.CMake`; das MSYS2-CMake verträgt sich nicht mit WinLibs-Ninja). Die CI führt sie unter Linux aus.
- Ein alter `build/wii`-Cache ist unproblematisch: Das CMake setzt die wii_tls-Krypto-Einstellungen per FORCE.
