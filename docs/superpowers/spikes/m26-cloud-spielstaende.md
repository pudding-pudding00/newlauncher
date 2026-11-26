# Spikes M26–M30: Cloud-Spielstände

Stand: 2026-11-26. Vorarbeit für die Spec `docs/superpowers/specs/2026-11-26-cloud-spielstaende-design.md`.
Fremder Quellcode wurde nur gelesen, nichts übernommen.

| Spike | Frage | Stand |
|---|---|---|
| S1 | NAND-Save-Ordner unter IOS 58 + AHBPROT lesen/schreiben | offen (Hardware) |
| S2 | AHBPROT/ISFS nach `--from-game` | offen (Hardware) |
| S3 | Schreibt Dolphin `/title/…/data` aus einer DOL? | offen |
| S4 | Speicherpfade der Emulatoren | teilweise (GX ja, N64/RetroArch offen) |
| S5 | Kann wii_tls v1.0.0, was die Anbieter brauchen? | **erledigt: ja** |
| S6 | App-Registrierungen | Nutzeraktion |
| S7 | MEGA-Rechenzeit auf der Wii | offen (Hardware) |

## S5: wii_tls v1.0.0 – Ergebnis positiv

Gelesen: `third_party/wii_tls/include/wiitls/http.h`, `src/http/wtls_http.c`, `src/http/wtls_http_req.c`.

- **Methoden:** `wtls_http_request.method` ist ein beliebiges RFC-9110-Token (PUT, POST, PATCH, DELETE …).
  POST/PUT/PATCH ohne Body senden `Content-Length: 0`.
- **Status:** Ein Status ≥ 400 ist kein Fehler; `wtls_http_status()` liefert ihn unverändert (401, 409, 412, 428).
- **Umleitungen:** `max_redirects = -1` folgt keiner Umleitung → 308 („Resume Incomplete“ bei Google) und 302
  (OneDrive-Download) kommen als Status an. Mit Folgen: `Authorization`, `Proxy-Authorization` und `Cookie`
  fallen **erst beim Wechsel des Ursprungs** weg, https → http wird abgelehnt (`WTLS_ERR_HTTP_REDIRECT`).
  303 sowie 301/302 nach POST werden zu GET ohne Body.
- **Header:** `wtls_http_get_header()` (erster Treffer, ohne Groß/klein), `wtls_http_header_at()` (alle);
  bis 64 eigene Header, `Host`/`Content-Length`/`Transfer-Encoding`/`Connection` setzt die Bibliothek selbst.
- **Upload:** `body`/`body_len` (Content-Length) oder `body_cb` mit `body_len` (Content-Length, Streaming) oder
  `body_chunked`. Ein Streaming-Body bricht bei Umleitungen, die den Body behalten (307/308), ab → für
  Uploads `max_redirects = -1`.
- **Wiederholung:** Nur idempotente Methoden (GET, HEAD, PUT, DELETE …) werden nach einem toten
  Keep-Alive-Socket automatisch neu gesendet, POST nicht.
- **Zufall:** `wtls_init()` ruft `psa_crypto_init()`; danach liefert `psa_generate_random()` CSPRNG-Bytes
  (wie `src/wtls_time.c:91`). Nur im Netz-Thread und nur im TLS-Build.
- **Kodierung:** `wtls_http_url_encode()` (RFC 3986) gibt es, ist aber nicht host-testbar → `core/url_encode`.

**Folge für den Plan:** kein Rückfall nötig; `httpSend` wird ein dünner Wrapper. Downloads mit eigener
Umleitungslogik (`max_redirects = -1`, `Location` selbst folgen) nur, wo ein Anbieter es verlangt.

## S4: Speicherpfade der Emulatoren – teilweise

Quellen: `dborth/snes9xgx`, `dborth/fceugx`, `dborth/vbagx` (`source/*.h`, `preferences.cpp`, `filebrowser.cpp`,
`sram.cpp`/`fceuram.cpp`/`vbasupport.cpp`), `emukidid/mupen64gc`, `extremscorner/not64`, `libretro/RetroArch`.

| Profil | Ordner (Standard) | Batterie-Save | Savestates (ausschließen) | Override |
|---|---|---|---|---|
| snes9xgx | `<gerät>:/snes9xgx/saves` | `<ROM-Datei ohne Endung>.srm`, dazu `.rtc` | `* Auto.frz`, `* N.frz`, `.png` | `settings.xml`: `saveFolder`, `saveDevice` |
| fceugx | `<gerät>:/fceugx/saves` | `<ROM ohne Endung>.sav` (FDS: keins) | `.fcs`, `.png` | dito |
| vbagx | `<gerät>:/vbagx/saves` (GB, GBC, GBA gemeinsam) | `<ROM ohne Endung>.sav` | `.sgm`, `.png` | dito |
| not64 | ungeklärt | ungeklärt | `/not64/saves/<goodname><region>.st0–9` | `settings.cfg` (Schlüssel ungeklärt) |
| wii64 | laut README `/wii64/saves` | ungeklärt (EEPROM/SRAM/FlashRAM/Mempak) | `<goodname><region>.st0–9` | `settings.cfg`: `NativeDevice` 0=SD, 1=USB |
| retroarch-* | ungeklärt (`savefile_directory`) | vermutlich `<ROM-Basisname>.srm` | `.state*` | `retroarch.cfg` |

- **GX-Gerätewahl:** `saveDevice` = Auto → erstes eingehängtes Gerät, auf dem `<app>/saves` schon existiert,
  sonst das erste eingehängte (SD vor USB). Der Locator bildet das nach: vorhandenen Ordner auf `sd:` vor `usb:`
  suchen.
- **`settings.xml`:** zuerst im eigenen App-Ordner (`apps/<app>/settings.xml`), dann `<app>/settings.xml`.
  Zahlenwerte von `saveDevice` sind ungeklärt → Locator liest nur `saveFolder`; das Gerät bleibt Auto-Suche.
- **N64 und RetroArch:** Quellen nicht eindeutig. Entscheidung: Diese Profile bekommen in M26 einen Pfad-Eintrag
  `save_dir`/`save_ext` je Profil in `emulators.ini` (Standard leer = „kein Abgleich“), und die
  Hardware-Abnahme prüft die tatsächlichen Dateien auf der SD-Karte des Nutzers. Danach werden die Standards
  nachgetragen.

## S1, S2, S3, S7

Noch offen. S1/S2/S7 brauchen die echte Wii (wiiload, Port 4299), S1-Schreibtests erst nach Rückfrage und
BootMii-NAND-Backup.
