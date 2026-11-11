# newlauncher v2 – universell, lizenzsauber, HTTPS

**Stand:** 2026-11-11 · **Basis:** `2026-11-09-wii-launcher-design.md` (im Folgenden „Basis-Spec“).
Diese Spec ergänzt die Basis-Spec. Widersprechen sich beide, gilt diese. Abschnitt 16 nennt jede Stelle der Basis-Spec, die sich ändert.

---

## 1. Ziel und Leitlinien

v1 ist für ein einziges Setup gebaut: eine Wii, eine FAT32-Platte, Szene-Standardordner, cIOS in Slot 249, Daten auf SD.
v2 soll auf jeder Wii mit Homebrew Channel laufen, ohne dass jemand seine Ordner umbauen muss. Außerdem schließt v2 die Lücken zu USB Loader GX und WiiFlow, die im Alltag zählen.

Leitlinien:

1. **Erkennen statt annehmen.** Geräte, Partitionen, cIOS, Emulatoren und Ordner werden erkannt. Feste Werte aus v1 werden zu Standardwerten.
2. **Alles bleibt überschreibbar.** Jede erkannte Annahme lässt sich in einer INI festlegen. Eine Festlegung schlägt die Erkennung. Die Erkennung schreibt nichts ungefragt, nur der Einrichtungs-Assistent (6.4) nach Bestätigung.
3. **Weniger Hardware ist kein Fehler.** Ohne SD, ohne USB, ohne Netz oder ohne GC-Ports bleibt der Launcher bedienbar. Er erklärt, was fehlt (Wii mini, vWii).
4. **Bestehende Installationen laufen weiter.** Eine `config.ini` aus v1 gilt unverändert. Alte Schlüssel werden weiter gelesen (16).
5. **Ehrliche Abnahme.** Was nur auf Hardware prüfbar ist, steht in der Abnahmeliste des Meilensteins. Was auf keiner verfügbaren Hardware geprüft wurde, heißt in den Release-Notes „ungeprüft“.
6. **Veröffentlichbar.** Jede verteilte Datei hat eine geklärte, GPLv3-verträgliche Lizenz (3).

## 2. Rahmenbedingungen v2 (ersetzt Basis-Spec 1.1 und 1.2)

- **Zweck:** allgemeiner Gebrauch. Andere installieren den Launcher in ihre bestehende Umgebung.
- **Plattformen:**
  - Wii (alle Revisionen);
  - Wii mini (keine SD, kein Netz, keine GC-Ports);
  - vWii (Wii U im Wii-Modus; Eingabe nur über Wii-Controller).
- **Startumgebung:** Homebrew Channel mit `<ahb_access/>`. Ohne AHBPROT laufen nur Emulator-Starts. Das prüft schon der Preflight aus v1.
- **Speicher:**
  - SD und USB, jeweils FAT32;
  - ab M16 zusätzlich NTFS, ext2/3/4 und WBFS-Partitionen;
  - mehrere Partitionen je USB-Gerät (primär und logisch, MBR und GPT).
  - exFAT wird erkannt und mit klarem Hinweis abgelehnt.
- **Testhardware:**
  - Dolphin;
  - die Wii des Entwicklers (RVL-001, USB-Platte FAT32);
  - weitere Konfigurationen nur, wenn jemand sie zur Verfügung stellt (13).

### 2.1 Nicht-Ziele v2

- Riivolution-Patches nativ anwenden. Vorher gepatchte ISOs starten wie normale Spiele.
- Echte Spielzeitmessung. Der Launcher läuft während des Spiels nicht. Es gibt nur Startanzahl und letzten Start (10.4).
- Wii-U-GamePad als Eingabe auf vWii.
- Wii-U-Pro-Controller, außer der Spike in M17 zeigt einen kleinen, sauberen Weg.
- EmuNAND anlegen oder befüllen. Nur vorhandene EmuNANDs werden genutzt.
- exFAT.
- Wii-Spiele als `.ciso`, `.wia` oder `.rvz`. Der Booter braucht eine lineare Sektorabbildung, siehe 5.3.
- Sega, PlayStation und weitere Konsolen; Mehrbenutzer; Forwarder-Kanal erzeugen.
- Sprachen außer Deutsch und Englisch. Die Texttabelle bleibt aber so gebaut, dass weitere ergänzt werden können.

## 3. Lizenz (entschieden)

### 3.1 Entscheidung

newlauncher bleibt **GPLv3 oder später**. Begründung:

- `wii_tls` und Mbed TLS stehen unter Apache-2.0. Apache-2.0 ist nur mit GPLv3 verträglich, nicht mit GPLv2.
- Der portierte WiiFlow-Lite-Code ist GPLv3 bzw. GPLv2-oder-später. Beides darf als GPLv3 verteilt werden.
- Code, der nur unter **GPLv2 ohne „oder später“** steht, ist mit GPLv3 nicht kombinierbar.

Solcher Code wird ersetzt, nicht umgedeutet. Dass WiiFlow Lite dieselbe Kombination seit Jahren verteilt, macht sie nicht sauber.

### 3.2 Bestand und Maßnahmen

| Teil | Lizenz | Maßnahme | Wann |
|---|---|---|---|
| `boot/source/main.cpp`, `Config.hpp` | GPLv3+ | bleibt | – |
| `boot/source/fst.c/h`, `patchcode.c/h` | GPLv2+ | bleibt, verteilt als GPLv3 | – |
| übrige `boot/source/*` ohne Kopf | WiiFlow Lite, GPLv3 | bleibt | – |
| `boot/source/ChannelHandler.cpp` | zlib-artig | bleibt; Änderungen im Kopf markieren | M11 |
| `boot/source/video_tinyload.c/h` | **GPLv2-only** | ersetzt durch eigenes `boot/nl/progress.c` (gleiche fünf Funktionen) | M11 |
| `boot/source/lz77.c/h` | **GPLv2-only** | ersetzt durch eigenes `core/lz77` mit C-Brücke `decompressLZ77content` | M11 |
| `boot/source/codehandler.h`, `codehandleronly.h` | Binär-Blob, Herkunft unklar | aus dem Build genommen (`NL_BOOTER_CHEATS=0`); in M15 aus belegter GPL-Quelle neu gebaut oder Wii-Cheats bleiben aus | M11 / M15 |
| libwbfs (upstream) | **GPLv2-only** | wird nicht übernommen; WBFS-Partitionen über eigenen Code auf `core/wbfs_map` | M16 |
| `third_party/wii_tls` | Apache-2.0 | vendort, `LICENSE` und Hinweise mitverteilt | M11 |
| Mbed TLS 4.1.1 (über `wii_tls`) | Apache-2.0 | Hinweis | M11 |
| Mozilla-CA-Bundle (in `wii_tls`) | MPL-2.0 | unverändert verteilt; Hinweis mit Quellverweis | M11 |
| M PLUS Rounded 1c | SIL OFL 1.1 | Hinweis, Lizenztext mitverteilt | M11 |
| libogc, libfat | zlib-artig / BSD-artig | Hinweis | M11 |
| FreeType | FTL (gewählt statt GPLv2) | Hinweis mit Credit-Satz der FTL | M11 |
| libpng, zlib, Tremor, libogg, bzip2, brotli | jeweils permissiv | Hinweis, soweit tatsächlich gelinkt | M11 |
| doctest | MIT | nur Tests, nicht verteilt | – |
| Cover, Metadaten, Cheats, Emulatoren, Nintendont | Dritte | **nicht** mitverteilt; lädt der Nutzer selbst | – |

**Ersatz ohne Abschreiben.** Für `progress.c` und `lz77` sind die ersetzten Dateien keine Vorlage:

- `progress.c` entsteht aus der Funktionsbeschreibung: schwarzer Bildschirm plus Fortschrittsbalken. Es nutzt die libogc-Video-API und die dokumentierten VI-Register.
- `lz77` entsteht aus der öffentlichen Formatbeschreibung (Nintendo-LZ77, Typen `0x10` und `0x11`, z. B. GBATEK) und Host-Tests.

Ein Review prüft, dass der Diff den alten Code nicht strukturell nachbildet.

### 3.3 Regeln für neue Abhängigkeiten

| Erlaubt | Abgelehnt |
|---|---|
| GPLv2-oder-später, GPLv3, LGPL 2.1+/3, Apache-2.0, MPL-2.0, MIT, BSD, zlib, Public Domain, CC0, OFL | GPLv2-only, LGPLv2-only ohne Umstiegsklausel, „nur nicht-kommerziell“, unklare Herkunft, Binär-Blobs ohne Quelle |

Jede neue Abhängigkeit braucht vor dem Einbau einen Eintrag in `THIRD_PARTY.md`: Name, Version, Lizenz, Quelle und welche Dateien verteilt werden.

### 3.4 Pflichten beim Verteilen

- `LICENSE` (GPLv3) im Repo und im Release-ZIP.
- `THIRD_PARTY.md` und Ordner `LICENSES/` mit allen mitverteilten Lizenztexten (Apache-2.0, MPL-2.0, OFL-1.1, FTL, zlib, BSD-Varianten, MIT).
- Release-ZIP: `apps/newlauncher/` enthält `LICENSE.txt` und `THIRD_PARTY.txt`.
- Einstellungen → „Über“: Version, Build-Optionen (`NL_TLS`), GPLv3-Hinweis, Liste der Drittkomponenten. Der Volltext liegt in der Datei, der Bildschirm zeigt Namen und Lizenzen.
- Quelltextangebot: Das Release verweist auf das Git-Tag. Vendorte Teile liegen im Repo, Mbed TLS als fest gehashte Quelle.
- CI-Prüfung `tools/check_licenses.py` schlägt fehl, wenn eine gelinkte Bibliothek oder ein Ordner unter `third_party/` keinen Eintrag in `THIRD_PARTY.md` hat.

## 4. Netz und TLS (M11)

### 4.1 Einbindung von wii_tls

- **Vendort statt Submodul:** `third_party/wii_tls/` enthält einen Stand des Projekts `wii_tls`.
  - Beide Repos sind privat. Ein Submodul bräuchte in der CI ein zusätzliches Zugriffstoken und bricht bei jedem öffentlichen Klon, solange `wii_tls` privat bleibt.
  - `tools/vendor_wii_tls.py <pfad-zum-repo> [--rev <commit>]` kopiert die versionierten Dateien des Commits ohne `tests/`, `bench/`, `examples/` und Build-Ordner. Es schreibt `third_party/wii_tls/VENDORED.txt` mit Commit, Datum und Quelle.
  - Ein Update ist ein Aufruf plus Commit.
- Mbed TLS lädt `wii_tls` per `FetchContent` mit festem SHA-256. Offline-Builds setzen `FETCHCONTENT_SOURCE_DIR_MBEDTLS`.
- CMake-Schalter `NL_TLS` (Standard `ON` für die Wii):
  - `add_subdirectory(third_party/wii_tls EXCLUDE_FROM_ALL)` mit `WTLS_PLATFORM=ogc`;
  - Tests, Hardware-Tests und Beispiele aus;
  - `target_link_libraries(... wiitls)`, Definition `NL_TLS=1`.
  - Die CI baut die Wii mit `NL_TLS=ON` und `OFF`.

### 4.2 Netz-Dienst

`wii_tls` hat globalen Zustand ohne Sperren: **alle `wtls_*`-Aufrufe aus einem einzigen Thread**. Daher:

- `platform/net_service` besitzt den einzigen Netz-Thread des Launchers: LWP, 64 KiB Stack, beim ersten Auftrag gestartet.
- Er ruft `net_init`, danach `wtls_init` (nur mit `NL_TLS`) und arbeitet eine Warteschlange von Aufträgen ab. Es läuft immer genau ein Auftrag. Das entspricht der Netz-Zeile in Basis-Spec 4.3.
- `NetWorker` (Cover) und `EmuWorker` (Emulatoren) behalten ihre Ablauflogik, starten aber keinen eigenen Thread mehr. Sie reichen Aufträge ein. Später kommen Cheats (M15) und die Zeitsynchronisierung dazu.
- Aufträge sind abbrechbar: Abbruch-Flag, geprüft zwischen Blöcken. `shutdown()` bricht ab, wartet auf das Ende und ruft `wtls_shutdown`. Das geschieht, bevor Laufwerke ausgehängt werden.

### 4.3 Transport

- `core/http`: `Url` bekommt `bool tls`. `parseUrl("https://…")` setzt `tls = true` und Port 443 (eigener Port erlaubt).
- `platform/http_client`: `request()` liest und schreibt über eine Transport-Schnittstelle (`send`, `recv`, `close`) mit zwei Umsetzungen:
  - Klartext-Socket (`net_*`);
  - TLS (`wtls_connect`, `wtls_read`, `wtls_write`, `wtls_close`).
- Zeitlimits: Verbindung 8 s, Handshake 10 s, Lesen/Schreiben 15 s. Werte aus `wtls_options`.
- **Kein Rückfall** von HTTPS auf HTTP. Zertifikatsfehler beenden den Auftrag.
- **Uhr:** Bei `WTLS_ERR_CLOCK` oder bei `WTLS_ERR_CERT` mit `MBEDTLS_X509_BADCERT_EXPIRED`/`_FUTURE` in `wtls_last_verify_flags()` läuft einmal pro Sitzung `wtls_time_sync_sntp(NULL, 3000)`, danach genau ein neuer Versuch. Der Log schreibt `NL_TLS_TIMESYNC ok|fail`.
- Ohne `NL_TLS` scheitert eine `https`-URL mit Marker `NL_TLS_MISSING` und dem Text „Diese Version kann kein HTTPS“.
- `[debug] tls_ca = <pfad.pem>` fügt über `wtls_add_trusted_ca` eine zusätzliche CA hinzu (Smoke-Test mit lokaler Test-CA). Es gibt **keine** Option, die Prüfung abzuschalten.

### 4.4 Fehlertexte (DE/EN)

| `wtls_status` | Text |
|---|---|
| `DNS` | Server nicht gefunden |
| `CONNECT`, `TIMEOUT` | Keine Verbindung zum Server |
| `CERT` | Zertifikat des Servers ungültig |
| `CLOCK` (nach Sync) | Uhrzeit der Wii stimmt nicht – bitte im Wii-Menü stellen |
| `HANDSHAKE` | Sichere Verbindung fehlgeschlagen |
| `NOMEM` | Zu wenig Speicher für die Verbindung |
| übrige | Netzwerkfehler (Code) |

### 4.5 Standardquellen

Mit TLS stellt M11 die Standardquellen auf HTTPS um. Das gilt aber nur, wenn der Server per HTTPS dieselben Daten liefert. Die Prüfung ist ein Schritt im M11-Plan. Sonst bleibt die HTTP-Adresse.

- GameTDB-Cover → `https://art.gametdb.com/…`. Damit entfällt das Basis-Spec-Risiko „GameTDB-Cover nur per HTTPS“.
- Open Shop Channel `[emulators] repo`.
- Nintendont bleibt beim Open Shop Channel. Eine GitHub-Quelle kommt nur dazu, wenn M11 dort eine stabile Download-Adresse mit Versionsangabe findet.
- Cheat-Quelle (M15).

### 4.6 Budgets

Zielwerte. Die M11-Abnahme misst sie auf Hardware; eine Abweichung wird dokumentiert, nicht still übernommen.

- Zusätzlicher Heap je TLS-Verbindung ≤ 160 KiB.
- Handshake ≤ 3 s.
- Die UI hält während eines Downloads ihre Bildrate (Basis-Spec 7).

## 5. Bibliothek universell (M12)

### 5.1 Datenordner

`<data>` ersetzt `sd:/newlauncher` aus Basis-Spec 5.1. Suchreihenfolge beim Start:

1. Ordner `newlauncher/` auf dem Gerät, von dem die App gestartet wurde. Das Gerät steht in `argv[0]`, z. B. `usb:/apps/newlauncher/boot.dol`.
2. `sd:/newlauncher/`
3. `newlauncher/` auf der ersten beschreibbaren USB-Partition
4. Fehlt der Ordner überall, wird er auf dem Startgerät angelegt, sonst auf SD, sonst auf USB.

Der Log schreibt `NL_DATA <pfad>`. Ohne beschreibbares Gerät läuft der Launcher nur lesend; die Einstellungen zeigen das.

### 5.2 Geräte und Quellen

- Beim Start werden SD und alle unterstützten USB-Partitionen eingehängt:
  - `usb:` ist die erste FAT-Partition, wie bisher;
  - weitere heißen `usb2:` bis `usb8:`;
  - ab M16 kommen NTFS und ext unter denselben Namen hinzu, WBFS-Partitionen als virtuelle Quellen (9.3).
- Partitionstabellen: MBR mit erweiterten Partitionen (EBR-Kette) und GPT.
  - Plattformfreier Parser `core/partition_table`.
  - Er erkennt den Typ am Kennbyte **und** am Bootsektor: FAT, NTFS, exFAT, ext, WBFS-Magic.
- `config.ini` `[library] sources = auto | <liste>`. `auto` heißt alle eingehängten Geräte mit Spielen. Eine Liste wie `usb,usb2,sd` legt Auswahl und Reihenfolge fest.
  - Der alte Schlüssel `[general] library = usb|sd` wird weiter gelesen und als `sources = usb` bzw. `sd` gedeutet.
- Gescannt wird je Gerät. Jedes Gerät hat seinen eigenen Index `<gerät>:/newlauncher/cache/index.bin` mit unverändertem Format.
  - Der Launcher führt die Indizes im Speicher zusammen. `Game` bekommt `u8 device`, einen Index in die Gerätetabelle der Sitzung.
- Liegt ein Spiel mit gleichem Schlüssel auf mehreren Geräten, erscheint es einmal, und zwar vom ersten Gerät der Quellenliste.
  - Die Detailansicht zeigt „Gerät: USB 1 (auch auf: SD)“.
- Cover- und Metadaten-Cache liegen beim Gerät des Spiels, wie bisher. Ist das Gerät nicht beschreibbar (NTFS/ext nur lesend, WBFS-Partition), liegen sie unter `<data>/cache/<gerät>/`.

### 5.3 Ordner und Dateinamen

`config.ini` `[paths]` legt pro Konsole Ordner fest:

- mehrere Ordner, durch `|` getrennt;
- relativ zur Wurzel jedes Quellgeräts;
- Groß-/Kleinschreibung wie im Dateisystem;
- durchsucht bis Tiefe 2 unter jedem Ordner.

Standardwerte:

| Konsole | Ordner (Standard) | Endungen (Standard) |
|---|---|---|
| Wii | `wbfs\|wii` | `.wbfs`, `.iso` |
| GameCube | `games\|gc` | `.iso`, `.gcm`, `.ciso`, `.nkit.iso`, FST-Ordner (`sys/boot.bin`) |
| N64 | `roms/n64` | `.z64`, `.n64`, `.v64` |
| NES, SNES, GB, GBC, GBA | `roms/<kürzel>` | wie Basis-Spec 5.3, zusätzlich `.zip`, wenn das gewählte Emulator-Profil sie annimmt (6.3) |

`[extensions] <kürzel> = .a|.b` ersetzt die Endungsliste einer Konsole.

**Wii-Spiele.** Erkannt werden diese Formen. Die ID6 kommt immer aus dem Disc-Kopf, der Dateiname liefert höchstens den Ersatztitel.

- `Titel [ID6]/ID6.wbfs|.iso` (+ `.wbf1` …)
- `ID6.wbfs|.iso`
- `ID6_Titel.wbfs|.iso`
- `Titel [ID6].wbfs|.iso`
- `Titel.wbfs|.iso`

**GC-Spiele:**

- `Titel [ID6]/game.iso` (+ `disc2.iso`)
- `Titel [ID6].iso`
- `ID6.iso`
- `.gcm`, `.ciso`, `.nkit.iso`
- entpacktes FST (`<ordner>/sys/boot.bin`)

Was Nintendont davon tatsächlich startet, prüft M12 im Nintendont-Quelltext. Nicht startbare Formen zeigt der Preflight mit Grund an.

**ROMs in `.zip`:**

- Die Konsole folgt aus dem Ordner, der Titel aus dem ZIP-Namen.
- Der Scanner prüft nur, dass das ZIP mindestens einen Eintrag mit passender Endung hat. Den Rest erledigt der Emulator.

### 5.4 Konsolentabelle

Die Konsolentabelle (Basis-Spec 5.3) bleibt im Code. Kürzel, Generation und Reihenfolge sind fest.

Ordner und Endungen kommen aus `[paths]`/`[extensions]`, sonst aus der Tabelle oben. `ConsoleInfo::folder` wird zu einer Liste.

## 6. System-Erkennung und Einrichtung (M13)

### 6.1 Systeminfo

`platform/system_probe` füllt einmal beim Start `SystemInfo`:

- **Plattform:**
  - Wii;
  - vWii: Latte-Kennung `(read32(0x0D8005A0) >> 16) == 0xCAFE`, nur mit AHBPROT lesbar; ohne AHBPROT gilt „unbekannt“ und das Verhalten wie Wii.
  - Die Wii mini wird nicht eigens erkannt. Fehlende SD, fehlendes Netz und fehlende GC-Ports zeigen sich einzeln.
- System-Menü-Version und Region (`0000000100000002`-TMD).
- IOS des Launchers (`IOS_GetVersion`) und AHBPROT.
- Gefundene cIOS (6.2) und Geräte (5.2): Name, Dateisystem, Größe, frei, Spiele.
- Erkannte Emulatoren (6.3) und Netzstatus (IP, Zeit synchronisiert).

Anzeige: Einstellungen → „Systeminfo“. Der Knopf „Als Datei speichern“ schreibt `<data>/sysinfo.txt` zum Weitergeben, **ohne** MAC-Adresse, Seriennummer oder Konsolen-ID.

### 6.2 cIOS

- Scan der Slots 200–255. Für jeden installierten Titel `00000001-000000xx` liest der Launcher TMD und Kopf des ersten Contents. Das ist die Logik aus M9 (`readCiosStatus`), verallgemeinert auf einen Slot-Parameter.
- Einordnung `CiosKind`:
  - `D2x`: `ios_info_t`-Magic, mit Basis und Version;
  - `Stub`: TMD-Titelversion `0xFF00` oder nur ein Content;
  - `Other`: installiert, aber nicht einzuordnen, z. B. Hermes oder Waninkoko.
- Wahl für ein Wii-Spiel, das erste Zutreffende gilt:
  1. `[game ID6] ios` in `games.ini`
  2. `[wii] ios` in `config.ini`
  3. automatisch: der erste d2x unter 249, 250, 248, sonst der d2x mit der höchsten Slotnummer, sonst 249, wenn dort `Other` liegt.
- Ohne passendes cIOS greift der vorhandene Dialog „cIOS fehlt“ aus M9.
- `bootWiiGame` nimmt den Slot aus dem Plan. `kCiosSlot` entfällt.

### 6.3 Emulatoren und Profile

- Die Erkennung durchsucht `<gerät>:/apps/*/` auf SD und allen FAT-USB-Partitionen nach `boot.dol` bzw. `boot.elf` und liest `<name>` aus `meta.xml`.
- Eine eingebaute Profiltabelle ordnet Ordner bzw. Namen Konsolen und Argument-Schemata zu: `fceugx`, `snes9xgx`, `vbagx`, `not64`, `wii64`, `nintendont`.
- **RetroArch:** Kern-DOLs in `apps/retroarch-wii/<kern>_libretro_wii.dol`, Argument `{path}`. Standardkerne:

  | Konsole | Kern |
  |---|---|
  | NES | `fceumm` |
  | SNES | `snes9x2010` |
  | GB, GBC | `gambatte` |
  | GBA | `mgba` |

  Ob RetroArch auf der Wii den ROM-Pfad als `argv[1]` annimmt, prüft der Spike in M13. Fällt er negativ aus, entfallen die RetroArch-Profile.
- Pro Konsole gilt eine Reihenfolge von Profilen. Das erste installierte gewinnt:
  - Standard für NES: `fceugx`, dann `retroarch`;
  - entsprechend für die anderen Konsolen.
- `emulators.ini` bleibt die stärkste Stufe: Ein vorhandener Abschnitt schlägt die Erkennung. Fehlt er, nimmt der Launcher den erkannten Pfad.
- Pro Spiel: `games.ini` `[emulator] <schlüssel> = <profil>`.
- N64 behält seine Auto-Wahl (Basis-Spec 6.6). Die Pfade von Not64/Wii64 kommen ebenfalls aus der Erkennung.
- M10 (Emulator-Verwaltung) installiert weiterhin nach `sd:/apps/`. Ohne SD installiert sie nach `apps/` auf `<data>`-Gerät.

### 6.4 Einrichtungs-Assistent

Startet, wenn `<data>/config.ini` fehlt. Erneut erreichbar über Einstellungen → „Einrichtung“.

Jeder Schritt zeigt das Erkannte und einen Vorschlag. A übernimmt, B überspringt mit Standardwert.

1. Sprache
2. Bibliotheksquellen: Geräte mit gefundenen Spielen je Konsole, Auswahl und Reihenfolge
3. cIOS: Tabelle aus 6.2 und Wahl
4. Emulatoren: gefundene Profile; fehlende kann man über M10 installieren, wenn Netz da ist
5. Netz: Verbindungstest, auf Wunsch Zeitabgleich
6. Fertig: Der Launcher schreibt `config.ini` und, wenn nötig, `emulators.ini`. Vorher erstellt er Sicherungen `*.bak`.

## 7. Spieloptionen und cIOS pro Spiel (M14)

### 7.1 Ablage und Vorrang

- Globale Standards stehen in `config.ini` `[wii]` und `[gc]`. Werte pro Spiel stehen in `games.ini` `[game <ID6>]`.
- Ein Schlüssel pro Spiel schlägt den globalen. Der Wert `default` oder ein fehlender Schlüssel bedeutet: globaler Wert.
- Unbekannte Werte fallen auf den Standard zurück. Der Log schreibt `NL_OPT_BAD <schlüssel>`.
- `core/game_options` löst die Werte plattformfrei auf und bildet sie auf Booter- bzw. `NIN_CFG`-Felder ab. Das ist vollständig host-getestet.

### 7.2 Wii (→ `the_CFG`)

| Schlüssel | Werte | Standard | Feld |
|---|---|---|---|
| `video` | `game`, `system`, `pal50`, `pal60`, `ntsc`, `progressive` | `system` | `vidMode` 0–5 |
| `video_patch` | `off`, `normal`, `more`, `all` | `off` | `patchVidMode` 0–3 |
| `vipatch` | `0`, `1` | `0` | `vipatch` |
| `language` | `game`, `ja`, `en`, `de`, `fr`, `es`, `it`, `nl`, `zh_s`, `zh_t`, `ko` | `game` | `configbytes[0]` (`0xCD` bzw. 0–9) |
| `country_patch` | `0`, `1` | `0` | `countryString` |
| `region_patch` | `0`, `1` | `0` | `patchregion` |
| `aspect` | `game`, `4:3`, `16:9` | `game` | `aspectRatio` −1/0/1 |
| `deflicker` | `game`, `off`, `off_ext`, `low`, `medium`, `high` | `game` | `deflicker` 0–5 |
| `width` | `auto`, `framebuffer` | `auto` | `videoWidth` 0/1 |
| `fix480p` | `0`, `1` | `0` | `patchFix480p` |
| `online` | `off`, `nossl`, `wiimmfi` | `off` | `private_server` 0–2 |
| `ios` | `auto`, `200`–`255` | `auto` | Slot (6.2) |
| `return_to` | `none`, `hbc`, 16 Hex-Ziffern | `none` | `returnTo` |

`return_to = hbc` nutzt die Title-ID des Homebrew Channels. Ob d2x die Rückkehr unterstützt, zeigt die Systeminfo; ohne d2x wird der Wert ignoriert.

### 7.3 GameCube (→ `NIN_CFG`, Version 10)

| Schlüssel | Werte | Standard |
|---|---|---|
| `video` | `auto`, `pal50`, `pal60`, `ntsc`, `mpal`, `none` | `auto` |
| `progressive` | `0`, `1` | `0` |
| `force_wide` | `0`, `1` | `0` |
| `wiiu_wide` | `0`, `1` (nur vWii wirksam) | `0` |
| `memcard` | `emu`, `multi`, `real` | `emu` |
| `memcard_blocks` | `59`, `123`, `251`, `507`, `1019`, `2043` | `251` |
| `native_si` | `0`, `1` | `0` |
| `language` | `auto`, `en`, `de`, `fr`, `es`, `it`, `nl` | `auto` |
| `max_pads` | `1`–`4` | `4` |
| `skip_ipl` | `0`, `1` | `0` |

Die Bitwerte kommen aus Nintendonts `common/include/CommonConfig.h`. M14 schreibt sie fest und nagelt sie im Test fest, wie M8 es für `MEMCARDEMU`, `AUTO_BOOT` und `USB` getan hat.

`memcard = real` setzt kein `MEMCARDEMU`. Ist kein GC-Port vorhanden (vWii, Wii mini), zeigt der Preflight einen Hinweis.

### 7.4 Oberfläche

- Detailansicht: neuer Knopf „Optionen“ (Wiimote `+`, GC-Controller `R`). Er öffnet eine Liste mit Links/Rechts-Wertewahl.
  - Erbt eine Zeile den globalen Wert, steht dort „Standard (…)“ mit dem geerbten Wert.
  - „Zurücksetzen“ löscht den Abschnitt des Spiels.
- Einstellungen: „Wii-Spiele“ und „GameCube-Spiele“ für die globalen Standards. Das ist dieselbe Szene ohne „Standard (…)“-Zeilen.
- Gespeichert wird beim Schließen der Szene.

## 8. Cheats und Patches (M15)

- **Codes holen:** Detailansicht → „Cheats“ → „Codes laden“ lädt eine Gecko-Textdatei nach `<data>/codes/<ID6>.txt`. Eigene Dateien darf man dort ablegen.
  - Quelle `config.ini` `[cheats] source`, Platzhalter `{id6}`.
  - Standard ist der GeckoCodes-Spiegel von RiiConnect24 per HTTPS. Die genaue Adresse prüft M15 vor der Festlegung.
  - Geladen wird nie automatisch.
- **Parser** (`core/gecko_txt`):
  - Kopf: Zeile 1 ID6, Zeile 2 Titel.
  - Danach Blöcke, getrennt durch Leerzeilen: Name (optional `[Autor]`), Codezeilen `XXXXXXXX YYYYYYYY`, Kommentarzeilen.
  - Codes mit Platzhaltern (`X`/`x` in Codezeilen) werden angezeigt, sind aber nicht wählbar.
- **Auswahl:** `<data>/codes/<ID6>.sel` speichert einen gewählten Codenamen pro Zeile. Ein Schalter `cheats = 0|1` in `[game ID6]` aktiviert die Auswahl.
- **GCT:** `core/gct` baut daraus `00D0C0DE 00D0C0DE`, die Codes und `F0000000 00000000`.
  - Die Größengrenze kommt aus dem Booter (`codelistend − codelist`) bzw. von Nintendont.
  - Eine Überschreitung ist ein Preflight-Fehler mit Angabe der Größe.
- **Wii:**
  - `the_CFG.cheats`/`cheatSize` zeigen auf die GCT im reservierten MEM2-Bereich.
  - `hooktype` aus `[game ID6] hook = vbi|kpad|joypad|gxdraw|gxflush|ossleep|axframe` (Booter-Werte 1–7), Standard `vbi`.
  - Voraussetzung ist ein Codehandler aus belegter GPL-Quelle. M15 beginnt mit diesem Spike.
    - Kandidaten: Gecko-OS-Quelle `codehandler.s` und Nintendonts Codehandler-Quelle, jeweils mit Lizenzprüfung.
    - Ohne Fund bleiben Wii-Cheats aus. Die Detailansicht nennt den Grund.
- **GC:** Nintendont-Flags `CHEATS` und `CHEAT_PATH`, `CheatPath` zeigt auf die GCT auf dem Spielgerät. Das genaue Pfadformat prüft M15 im Nintendont-Quelltext.
- **WIP-Patches:**
  - `<data>/codes/<ID6>.wip`, Zeilen `offset original neu` in Hex.
  - Werden als `wip_list` an den Booter übergeben (Struktur aus `booter_cfg.h`).
  - Kaputte Zeilen werden übersprungen und geloggt.
- Riivolution: Nicht-Ziel (2.1).

## 9. Dateisysteme (M16)

### 9.1 Spike zuerst

M16 beginnt mit einem Spike. Er beantwortet je Bibliothek (libntfs-Wii-Port, libext2fs-Wii-Port) diese Fragen:

- Lizenz je Datei (3.3);
- baut mit dem aktuellen devkitPPC;
- liest auf der Wii stabil;
- liefert Datei-Extents, also eine Sektorliste je Datei für die Fragmentliste von Wii-Spielen.

Fällt eine Bibliothek durch, entfällt dieses Dateisystem in v2. Der Launcher erkennt es dann und meldet „nicht unterstützt“.

### 9.2 NTFS und ext2/3/4

- Vendort unter `third_party/` mit `VENDORED.txt`. Eingehängt wird **nur lesend**, denn Schreiben auf NTFS/ext birgt auf der Wii ein Risiko für die Daten.
- Caches liegen dann unter `<data>/cache/<gerät>/` (5.2).
- Wii-Spiele: Die Fragmentliste entsteht aus den Extents des Dateisystems. Das ist dieselbe `FilePart`-Schnittstelle wie bei FAT (`core/frag_list`), samt Gegenprobe aus M9.
- GC-Spiele: Nintendont liest nur FAT. Der Preflight meldet „Nintendont braucht FAT32“.
- ROMs: Ein Emulator-Profil erklärt `fs = fat` (Standard) oder `fat|ntfs`. Sonst zeigt der Preflight einen Hinweis.

### 9.3 WBFS-Partitionen

- Erkennung: Magic `WBFS` im ersten Sektor der Partition.
- Lesen über `core/wbfs_map`. Der Partitionskopf ist derselbe wie im `.wbfs`-Dateikopf, nur mit mehreren Disc-Slots. `parseWbfsHead` bekommt einen Slot-Parameter.
- Fragmentliste: Die Partition ist ein einziges Extent ab ihrer Start-LBA. Dann gilt `buildWbfsFragList` unverändert.
- Spielpfad im Index: `wbfs:<slot>`. Scan, Cover und Metadaten laufen wie bei Dateien.
- Nur lesen: Spiele installieren, löschen oder umbenennen bleibt Sache von PC-Werkzeugen.

## 10. Eingabe und Komfort (M17)

### 10.1 Classic Controller

Classic Controller und Classic Controller Pro bekommen eine eigene Spalte in Basis-Spec 3.8:

| Aktion | Belegung |
|---|---|
| Navigieren | Steuerkreuz, linker Stick |
| Bestätigen / Zurück | A / B |
| Konsole wechseln | L / R |
| Favorit | Y |
| Suche | X |
| Optionen | + |
| HOME-Menü | HOME |
| Debug-Overlay | ZL + ZR |

Der Wii-U-Pro-Controller ist ein Spike ohne Zusage (2.1).

### 10.2 Themes

- `<data>/themes/<name>/theme.ini` mit den Abschnitten `[light]` und `[dark]`. Darin stehen die 14 Tokens aus Basis-Spec 3.6 als `#RRGGBB` bzw. `#RRGGBBAA`.
- Optional `background.png`, höchstens 640×480; wird beim Laden in eine Textur umgewandelt.
- Fehlende oder kaputte Werte nehmen das Standard-Token. Der Log schreibt `NL_THEME_BAD <token>`.
- Einstellung „Theme“: Standard oder einer der gefundenen Ordner. Schriften bleiben fest.

### 10.3 Kategorien

- **Genres:** `prepare_meta.py` schreibt die GameTDB-Genres in `meta.bin`. Das Format bekommt eine neue Version und bleibt rückwärts lesbar.
- **Eigene Kategorien:** `<data>/categories.ini` mit `[<Kategorie>]` und einem Spiel-Schlüssel pro Zeile.
- **Filter:** im Such- und A–Z-Overlay eine Filterzeile: Alle, Favoriten, Genres, eigene Kategorien. Die Detailansicht ordnet eigene Kategorien zu.

### 10.4 Statistik

- `<data>/stats.ini`: `[<schlüssel>]` mit `starts` (Anzahl) und `last` (Unix-Zeit), geschrieben vor jedem Start.
- Die Detailansicht zeigt „12-mal gestartet, zuletzt am 03.11.2026“.
- Neue Sortierungen: „Zuletzt gespielt“, „Meistgespielt“.

### 10.5 Kindersicherung

- Einstellungen → „Kindersicherung“:
  - PIN mit 4 Ziffern; gespeichert als Hash mit Salz, das ist ein Kinderschloss, kein Tresor;
  - Altersgrenze nach Bewertungssystem (USK, PEGI, ESRB, CERO) aus den GameTDB-Daten in `meta.bin`;
  - Sperre pro Spiel (`[game <schlüssel>] locked = 1`).
- Gesperrte Spiele zeigen ein Schloss. Ein Start, die Einstellungen der Kindersicherung und „Einrichtung“ verlangen die PIN.
- Option „Wii-Jugendschutz übernehmen“: liest Stufe und PIN aus `SYSCONF` (`IPL.PC`). Das Format prüft der Spike in M17. Fällt er negativ aus, entfällt die Option.
- Spiele ohne Bewertung gelten als freigegeben. Eine Option kann sie als gesperrt behandeln.

## 11. Banner (M18)

M18 beginnt mit einem Spike, der Umfang und Machbarkeit festlegt. Danach kommt in dieser Reihenfolge:

1. **GC-Banner (statisch):** `opening.bnr` (BNR1/BNR2) aus dem FST des ISO. Bild 96×32 RGB5A3 als Textur in der Detailansicht. Kein Spike nötig.
2. **Kanal-Banner aus dem NAND** (VC-Injects, N64): IMET-Kopf → U8-Archiv → `banner.bin` (LZ77 aus M11) → BRLYT/BRLAN/TPL.
   - Gerendert wird eine Teilmenge: Bild-Panes, Keyframes für Position, Skalierung, Alpha und Textur-SRT, Schleife.
   - Ton aus `sound.bin` (BNS, DSP-ADPCM, eigener Dekoder) über ASND.
   - Rückfallebene: erstes Bild ohne Animation.
3. **Wii-Disc-Banner:** Sie liegen in der verschlüsselten Spielpartition.
   - Nur wenn der Spike einen sauberen Weg findet, den Titelschlüssel unter dem HBC-IOS zu entschlüsseln. Das darf weder den Common Key im Quelltext brauchen noch ein Fremd-Tool.
   - Sonst Nicht-Ziel.
- **Abspielen:** in der Detailansicht nach 1 s Fokus; der Banner-Ton dämpft die Musik und endet beim Verlassen.
- **Budget:** ein Banner zugleich, ≤ 4 MiB MEM2 einschließlich Texturen, Laden im Cover-Thread.

## 12. Weitere Startwege (M19)

### 12.1 Disc

- Erkennung über `/dev/di`: Disc-ID lesen, auf Aktion oder beim Scan.
- Eine eingelegte Disc erscheint als erster Eintrag der passenden Konsole, markiert mit „Disc“.
- **Wii-Disc:** Booter `TYPE_WII_DISC` über das gewählte cIOS. Die Spieloptionen aus 7.2 gelten.
- **GC-Disc:** Nintendont startet von Disc, soweit Nintendont das unterstützt. Der Spike in M19 prüft das. Nicht auf vWii und nicht auf der Wii mini.

### 12.2 EmuNAND (nur nutzen)

- `config.ini` `[emunand] path = <gerät>:/<ordner>`; ohne den Schlüssel gibt es keine EmuNAND-Funktionen.
- `[game ID6] save = real|emu`, Standard `real`. `emu` schaltet die d2x-NAND-Emulation für das Spiel ein. Der Port der Einschaltlogik aus WiiFlow Lite (GPLv3) ist Teil von M19.
- Kanäle aus dem EmuNAND erscheinen unter einer neuen Konsole „Kanäle“.
  - Titel aus `<path>/title/00010001/*/content/title.tmd`, Namen aus dem IMET-Kopf.
  - Start über Booter `TYPE_CHANNEL` mit eingeschalteter Emulation.
  - LZ77 kommt aus M11.
- Anlegen und Befüllen sind Nicht-Ziel.

### 12.3 vWii

- Startwege wie auf der Wii. Die Systeminfo zeigt „vWii“.
- GC-Spiele über Nintendont mit `wiiu_wide`. Der GC-Controller-Hinweis entfällt.
- Wii-U-GamePad: Nicht-Ziel.

## 13. Abnahme

- Jeder Meilenstein liefert:
  - Host-Tests für alles in `core/`;
  - Dolphin-Smokes, wo Dolphin das Verhalten zeigt;
  - eine Hardware-Liste (W-Punkte), die der Nutzer auf der Wii abarbeitet.
- **Hardware-Matrix:**

  | Konfiguration | Status |
  |---|---|
  | Wii + USB FAT32 | Pflicht |
  | Wii ohne SD | Pflicht, SD herausnehmen |
  | Wii + zweite USB-Partition | Pflicht, wenn die Platte das zulässt |
  | vWii, Wii mini, NTFS, ext4, WBFS-Partition | optional, nur mit verfügbarer Hardware bzw. Testplatte |

  Nicht geprüfte Felder stehen als „ungeprüft“ in den Release-Notes.
- **Regression:** Nach jedem Meilenstein bleiben alle Smokes aus M0–M10 grün, und eine v1-`config.ini` startet ohne Änderung.

## 14. Meilensteine

Jeder Meilenstein hat einen eigenen Plan unter `docs/superpowers/plans/2026-11-11-mNN-*.md`.

| # | Inhalt | Hängt ab von | Abnahme |
|---|---|---|---|
| **M11** | Lizenz und TLS: `progress.c`, `core/lz77`, Codehandler aus dem Build, `THIRD_PARTY.md`/`LICENSES/`/„Über“, `check_licenses.py`; `wii_tls` vendort, Netz-Dienst, Transport, HTTPS-Standardquellen. Ersetzt M10 Task 9 | – | Host, Dolphin, Wii |
| **M12** | Bibliothek universell: `<data>`, Partitionstabellen, mehrere Geräte, Quellenliste, flexible Ordner und Namen, ZIP-ROMs | M11 | Host, Dolphin, Wii |
| **M13** | System-Erkennung: Systeminfo, cIOS-Scan und Wahl, Emulator-Erkennung, RetroArch-Profile (Spike), Einrichtungs-Assistent | M12 | Host, Dolphin, Wii |
| **M14** | Spieloptionen Wii und GC, cIOS pro Spiel, Options-Szene | M13 | Host, Dolphin (Dry-Run), Wii |
| **M15** | Cheats (Spike Codehandler), GCT, WIP | M11, M14 | Host, Dolphin, Wii |
| **M16** | Dateisysteme (Spike), NTFS/ext lesend, WBFS-Partitionen | M12 | Host, Wii |
| **M17** | Classic Controller, Themes, Kategorien, Statistik, Kindersicherung | M12 | Host, Dolphin, Wii |
| **M18** | Banner (Spike): GC statisch, Kanal-Banner, Wii-Disc nach Spike | M11 | Host, Dolphin, Wii |
| **M19** | Disc-Start, EmuNAND, vWii-Feinschliff | M13, M14, M18 | Wii |

Begründung der Reihenfolge:

- M11 zuerst, weil es jede Veröffentlichung freigibt und HTTPS für Cover und Cheats liefert.
- M12 vor M13, weil die Erkennung das Gerätemodell braucht.
- M14 vor M15, weil die Options-Szene die Cheat-Schalter aufnimmt.
- M16 bis M18 sind untereinander unabhängig.
- M19 zuletzt, weil es auf cIOS-Wahl, Optionen und LZ77/IMET aufbaut.

## 15. Risiken

| Risiko | Prüfung in | Rückfallebene |
|---|---|---|
| `wii_tls` verhält sich auf echter Hardware anders als in den Host-Tests (Entropie, Uhr, Speicher) | M11 W-Liste, zusammen mit den Hardware-Suiten von `wii_tls` | `NL_TLS=OFF`-Build bleibt veröffentlichbar; HTTP-Quellen bleiben konfigurierbar |
| HTTPS-Server liefern andere Daten oder verlangen SNI/ALPN-Sonderfälle | M11 Task „Standardquellen“ | Standard bleibt HTTP für diese Quelle |
| Eigenes `progress.c` stört das Video-Timing des Booters | M11 W-Liste | Fortschrittsbalken weglassen (schwarzer Bildschirm) |
| Mehrere USB-Partitionen: libfat-`fatMount` mit Startsektor verhält sich auf der Wii anders | M12 W-Liste | nur erste Partition; weitere in der Systeminfo als „nicht eingehängt“ |
| cIOS-Einordnung irrt bei exotischen Builds | M13 | `ios` in `config.ini`/`games.ini` festlegen |
| RetroArch nimmt `argv` auf der Wii nicht an | M13 Spike | RetroArch-Profile entfallen |
| `NIN_CFG`-Bits weichen zwischen Nintendont-Versionen ab | M14 | Version aus dem Binary (M8) begrenzt die angebotenen Optionen |
| Kein Codehandler mit belegter Lizenz | M15 Spike | Wii-Cheats aus; GC-Cheats laufen über Nintendont |
| NTFS-/ext-Ports nicht baubar oder ohne Extents | M16 Spike | Dateisystem entfällt, klare Meldung |
| BRLYT/BRLAN zu umfangreich | M18 Spike | statisches erstes Bild |
| d2x-NAND-Emulation nicht sauber portierbar | M19 | EmuNAND entfällt |

## 16. Änderungen an der Basis-Spec

- **1.1/1.2:** ersetzt durch Abschnitt 2 dieser Spec; dort steht ein Verweis.
- **2, Zeile „Lizenz“:** „GPLv3 oder später; GPLv2-only-Teile ersetzt, siehe v2-Spec 3“.
- **5.1:** `sd:/newlauncher` heißt `<data>` (5.1 dieser Spec). Pfade unter `<lib>` gelten je Quellgerät.
- **5.3:** Ordner und Endungen sind Standardwerte (5.3/5.4 dieser Spec).
- **5.4:** Neue Schlüssel:
  - `config.ini`: `[library] sources`, `[paths]`, `[extensions]`, `[wii]`, `[gc]`, `[cheats] source`, `[emunand] path`, `[parental]`, `[theme] name`, `[debug] tls_ca`;
  - `games.ini`: `[game <ID6>]`, `[emulator]`.
  - `[general] library` bleibt als Alt-Schlüssel lesbar.
- **9:** Zeilen M11–M19 mit Verweis auf Abschnitt 14 dieser Spec.
- **10:** Die Zeile „GameTDB-Cover nur per HTTPS“ ist mit M11 erledigt; die Risiken aus Abschnitt 15 kommen dazu.
- **M10-Plan Task 9:** geht in M11 auf.
