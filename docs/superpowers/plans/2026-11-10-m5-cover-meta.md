# M5 – Cover und Metadaten: Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Der Hauptbildschirm und die Detailansicht zeigen echte Cover. Auf dem PC füllen zwei Skripte die Metadaten (`meta.bin`) und die Retro-Cover. Auf der Wii lädt die Aktion „Fehlende Cover laden“ die Wii-/GC-Cover von GameTDB.

**Architecture:** Alles Prüfbare liegt plattformfrei in `core/`:
- CMPR-Encoder
- `.tex`-Format
- Einpassen auf 200×280
- Cover-Pfade
- Cover-LRU mit Byte-Budget und Prioritäten
- HTTP-Anfrage und Antwort-Parser
- Download-Logik (Regionen, Aufträge)

Auf der Wii:
- `platform/png_decode.cpp`: libpng
- `platform/cover_worker.cpp`: Cover-Thread lädt `.tex` oder konvertiert PNG → `.tex`
- `platform/http_client.cpp` und `platform/net_worker.cpp`: Netz-Thread, nur bei Bedarf
- Der Renderer zeichnet Cover-Batches mit eigener TEV-Stufe.

Die UI kennt Cover nur über ein `CoverProvider`-Interface und bleibt so host-testbar.

**Tech Stack:** wie M4. Dazu kommen libpng und zlib aus den devkitPro-Portlibs (`ppc-libpng`), die libogc-Sockets (`network.h`) und `CONF_GetLanguage`. Auf dem PC Python 3 mit Standardbibliothek (`xml.etree`, `zlib`, `urllib`).

**Spec:** `docs/superpowers/specs/2026-11-09-wii-launcher-design.md`, Abschnitte:
- §3.1 (Cover-Raster), §3.2 (großes Cover), §3.4 (Cover-Region, Aktion „Fehlende Cover laden“)
- §4.1, §4.3 (Cover- und Netz-Thread), §4.4 (Cover-LRU 16 MB, MEM2, 32-Byte-ausgerichtet)
- §5.1, §5.2 (Cover-Datei, Metadaten-Schlüssel), §5.5 (`meta.bin`, `.tex`)
- §6.1 Schritt 7, §6.3, §6.4, §7 (Cover ≤ 100 ms, keine UI-Blockade)
- §8.1 (CMPR-Encoder testgetrieben), §9 M5, §10 (GameTDB nur per HTTPS?)

**Basis:** Branch `feat/m5-cover-meta` von `main` (enthält M4, Merge `a106545`).

## Global Constraints

- Alle Global Constraints aus M1–M4 gelten weiter:
  - C++20 ohne Exceptions und RTTI, `Result`
  - keine Allokation im Leerlauf (`NL_ALLOC_IDLE 0`)
  - `core/` ohne `<ogc/...>`
  - Dolphin nur mit `-u`; die private Dolphin-Konfiguration bleibt unberührt
  - keine ROMs/ISOs/WADs im Repo, nur Dummy-Fixtures; Fixture-Cover sind selbst erzeugte Einfarb-PNGs
  - Commits enden mit `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`
- Cover-Größe **200×280**, Format CMPR (`gx_format` 14), Daten 28 000 Byte.
- `.tex` (Spec §5.5), Big-Endian:
  - Kopf: `"NLTX"` | `u16 breite` | `u16 höhe` | `u32 gx_format` | `u32 datenlänge`, danach Nullen bis Offset 32
  - Daten ab Offset 32
- Pfade (Spec §5.1/§5.2), `<lib>` = Bibliotheksgerät:
  - Quelle: `<lib>:/newlauncher/covers_src/<konsole>/<stamm>.png`
  - Cache: `<lib>:/newlauncher/cache/covers/<konsole>/<stamm>.tex`
  - `<stamm>`: ID6 bei Wii/GC, sonst der Dateiname ohne letzte Endung
- GameTDB-URL: `http://art.gametdb.com/wii/cover/<REGION>/<ID6>.png`. Am 2026-11-10 geprüft: HTTP liefert 200 `image/png` ohne Umleitung. Das Risiko „nur HTTPS“ aus §10 trifft also nicht zu.
- Region-Reihenfolge:
  - `auto`: Systemsprache (DE/FR/JA, sonst EN) → `EN` → `US` → `JA`, ohne Doppelte
  - feste Region: diese zuerst, dann dieselbe Kette
- Cover-LRU: Budget 16 MiB (`16 * 1024 * 1024`), Texel 32-Byte-ausgerichtet. Ein im aktuellen Frame gewünschtes Cover wird nie verdrängt.
- Einblendung 150 ms linear, sobald ein Cover bereit ist. Vorladen: sichtbare Reihen plus zwei Reihen darüber und darunter.
- Netz: Verbindungs- und Lese-Timeout 10 s. Antwort höchstens 2 MiB, sie muss mit der PNG-Signatur beginnen.
- Neue Log-Marker:
  - `NL_COVER_CONVERTED <konsole>/<stamm>`: PNG → `.tex` geschrieben
  - `NL_COVER_FAIL <konsole>/<stamm> <grund>`: PNG kaputt, zu groß o. Ä.
  - `NL_COVERS ready=<n>`: einmal, sobald nach `NL_READY` keine Anfrage mehr offen ist
  - `NL_NET ok` / `NL_NET_FAIL <code>`
  - `NL_COVER_DL <ID6> <REGION> ok|missing|fail`
  - `NL_COVER_DL_DONE ok=<n> missing=<m> failed=<k>`
  - `NL_CMD fetch_covers`
- Test-Schalter in `config.ini` (nur Fixtures/Smoke):
  - `[debug] cover_url = <basis>`: ersetzt `http://art.gametdb.com/wii/cover`
  - `[debug] fetch_covers = 1`: startet den Download nach `NL_READY`

## Review Focus

1. **Kaputte oder feindliche Dateien:**
   - `.tex` mit falscher Länge, Größe ≠ 200×280, fremdem Format oder abgeschnitten
   - PNG abgeschnitten, 1×1, 10000×10, Palette/Graustufen/16 Bit/Interlaced
   - Erwartet: verworfen bzw. neu erzeugt, oder `NL_COVER_FAIL` und Platzhalter, nie Absturz (Tests in Task 1, 2 und 8)
2. **LRU unter Druck:** mehr sichtbare plus vorgeladene Cover als das Budget fasst, schnelles Scrollen, Bibliothekswechsel nach dem Scan. Erwartet:
   - nur nicht gewünschte Einträge werden verdrängt
   - das Budget wird höchstens um die gerade gewünschten Cover überschritten
   - keine hängenden `Pending`-Einträge (Tests in Task 3)
3. **HTTP-Antworten:**
   - 404, 301 auf HTTPS, `Transfer-Encoding: chunked`, fehlende `Content-Length`
   - Antwort > 2 MiB, HTML statt PNG mit 200, Header über mehrere `recv`-Stücke verteilt
   - Erwartet: richtig dekodiert oder sauber als `missing`/`fail` gewertet (Tests in Task 4)
4. **Namen mit Sonderzeichen:**
   - Dateinamen mit `.` im Titel (`Super Mario Bros. (World).nes`), `&`, `'`, Umlauten
   - libretro-Ersetzung von `&*/:\`<>?\|"` durch `_`
   - Erwartet: richtiger Stamm, richtige URL, richtiger Metadaten-Schlüssel (Tests in Task 2, 5 und 6)
5. **Kein Netz oder kein Server:** `net_init` scheitert, DNS scheitert, Verbindung verweigert. Erwartet: Zusammenfassung mit `failed=n`, UI bleibt bedienbar, ein zweiter Lauf ist möglich (Test in Task 4, Abnahme in Task 9).

## Abweichungen und Festlegungen gegenüber der Spec

| Punkt | Festlegung in M5 | Grund |
|---|---|---|
| PNG-Dekoder | libpng aus den Portlibs statt `stb_image`; nur PNG, kein JPG | libpng liegt als FreeType-Abhängigkeit schon vor; GameTDB und libretro-thumbnails liefern nur PNG. JPG ließe sich später ergänzen |
| Zugang zu „Fehlende Cover laden“ | Die HOME-Kachel „Einstellungen“ öffnet bis M6 eine kleine Aktionsliste: „VC-Injects zuordnen“, „Fehlende Cover laden“ | Die Einstellungsszene folgt erst mit M6 |
| Veraltete `.tex` | Gibt es eine `.tex`, wird sie benutzt, auch wenn die PNG neuer ist. Neu konvertieren: `cache/covers` löschen (README) | `FileSystem` kennt kein `stat`; v1 braucht das nicht |
| Vorladen | zwei Reihen über **und** unter dem sichtbaren Bereich | Die Scrollrichtung kennt die Anfrage nicht; der Aufwand bleibt klein |
| Titel aus `wiitdb.xml` | `prepare_meta.py --lang` (Standard `DE`, Rückfall `EN`) | Der PC kennt die Wii-Systemsprache nicht |
| Retro-Metadaten | libretro-`metadat/releaseyear` und `metadat/maxusers` (clrmamepro-DAT); Abgleich über den No-Intro-Namen (`comment`), sonst über die CRC32 der Datei | Die DATs sind Text und lassen sich ohne Zusatzpakete lesen |
| `wiitdb.xml` | Pfad als Parameter `--wiitdb`, kein automatischer Download | `gametdb.com/wiitdb.zip` liefert ohne Browser nur HTML (geprüft am 2026-11-10) |
| Cover-Download im PC-Skript für Wii/GC | entfällt | Risiko §10 trifft nicht zu (HTTP geht) |

## Dateistruktur

```
src/core/
  cmpr.hpp/.cpp             encodeCmpr (RGBA8 → CMPR, GX-Kachelreihenfolge)
  tex_file.hpp/.cpp         writeTex / parseTex (.tex, Spec 5.5)
  cover_image.hpp/.cpp      fitCover (RGBA beliebig → 200×280, transparente Ränder)
  cover_paths.hpp/.cpp      coverStem, coverSrcPath, coverTexPath
  cover_cache.hpp/.cpp      CoverCache (LRU, Budget, Prioritäten, Einblend-Alpha)
  http.hpp/.cpp             buildGetRequest, parseUrl, HttpParser (Content-Length, chunked)
  cover_download.hpp/.cpp   coverRegions, coverDownloadJobs, gametdbCoverUrl, DownloadSummary
  config.hpp/.cpp           + debugCoverUrl, debugFetchCovers
src/gfx/
  draw_list.hpp             TextureId → u16; kCoverTextureBase
  texture_table.hpp         CoverTextures-Interface für den Renderer
  renderer.hpp/.cpp         Cover-Batches (CMPR, TEV MODULATE)
src/platform/
  png_decode.hpp/.cpp       decodePng (libpng → RGBA8)
  cover_worker.hpp/.cpp     Cover-Thread
  http_client.hpp/.cpp      httpGet über libogc-Sockets
  net_worker.hpp/.cpp       Netz-Thread „Fehlende Cover laden“
src/ui/
  cover_provider.hpp        CoverProvider, CoverView
  main_scene.*, details_scene.*   Cover zeichnen, Anfragen stellen
  actions_scene.hpp/.cpp    Aktionsliste aus der HOME-Kachel „Einstellungen“
  home_menu.*, scene.hpp, strings.hpp   OpenActions/FetchCovers, Texte
src/app/main.cpp, selftest.*        Verdrahtung, Selbsttest
tools/
  pngfile.py                minimaler PNG-Schreiber (Fixtures, Tests)
  prepare_meta.py           meta.bin aus wiitdb.xml und libretro-DATs
  prepare_covers.py         Retro-Boxarts aus libretro-thumbnails
  make_fixtures.py, deploy_dolphin.py, dolphin_smoke.py   Fixture-Cover, Cache leeren, Cover-Server
.github/workflows/build.yml         ppc-libpng
```

---

### Task 1: CMPR-Encoder und `.tex`-Format

**Files:**
- Create: `src/core/cmpr.hpp/.cpp`, `src/core/tex_file.hpp/.cpp`
- Test: `tests/host/core/cmpr_test.cpp`, `tests/host/core/tex_file_test.cpp`
- Modify: `CMakeLists.txt` (Core-Quellen)

**Interfaces:**
- Produces:
  - `inline constexpr u32 kGxFormatCmpr = 14`
  - `std::vector<u8> encodeCmpr(std::span<const u8> rgba, u16 width, u16 height)`:
    - `rgba` mit 4 Byte pro Pixel, zeilenweise
    - Breite und Höhe sind Vielfache von 8 (sonst leeres Ergebnis)
    - Ergebnisgröße `w*h/2`
  - `inline constexpr u16 kCoverW = 200, kCoverH = 280`
  - `inline constexpr u32 kTexHeaderBytes = 32`
  - `std::vector<u8> writeTex(u16 w, u16 h, std::span<const u8> cmpr)`
  - `struct TexView { u16 width, height; std::span<const u8> data; }`
  - `Result<TexView> parseTex(std::span<const u8> file)`:
    - prüft Magic, Format 14, `datenlänge == w*h/2` und Dateilänge ≥ 32 + Länge
    - liefert `ErrorCode::InvalidData` bei Abweichung

- [ ] **Step 1: Write the failing tests:**
  - `TEST_CASE("CMPR: einfarbiger Block")`: 8×8 komplett `#FF0000FF`.
    - Ergebnis 32 Byte
    - jeder der vier 8-Byte-Unterblöcke beginnt mit RGB565 `0xF800` (Big-Endian)
    - alle Indizes zeigen auf diese Farbe
    - Farbmodus mit 4 Farben, also `c0 > c1`; bei identischen Farben `c1 = c0 - 1` bzw. `c0 = c1 + 1` wählen, Test pinnt `c0 > c1`
  - `TEST_CASE("CMPR: Unterblock-Reihenfolge")`: 8×8, Quadranten links oben rot, rechts oben grün, links unten blau, rechts unten weiß. Die Unterblöcke 0–3 haben die Farben in genau dieser Reihenfolge.
  - `TEST_CASE("CMPR: Transparenz erzwingt 3-Farben-Modus")`:
    - 4×4-Quadrant mit einem Pixel Alpha 0, Rest grau
    - der Unterblock hat `c0 <= c1`
    - der Index dieses Pixels ist 3: Zeilenbyte MSB zuerst, Pixel 0 in Bits 7–6
  - `TEST_CASE("CMPR: Kachelreihenfolge 16×8")`: Der erste 32-Byte-Block gehört zu x 0–7, der zweite zu x 8–15.
  - `TEST_CASE("CMPR: ungültige Größe")`: 12×8 ergibt ein leeres Ergebnis.
  - `TEST_CASE(".tex Round-Trip")`: `parseTex(writeTex(200, 280, daten))` liefert 200, 280 und dieselben Daten; Datenoffset 32.
  - `TEST_CASE(".tex verwirft Kaputtes")`:
    - falsches Magic, Format 6
    - Länge ≠ w·h/2
    - Datei kürzer als Kopf + Länge
    - leere Datei
    - alles `isErr()`

- [ ] **Step 2: Run, expect FAIL** (Kompilierfehler: Header fehlen).
  Run: Host-Build und -Tests (`cmake --build --preset host && ctest --preset host`, mit MinGW im PATH).

- [ ] **Step 3: Implement.**
  - Encoder pro 4×4-Block:
    - Endpunkte: min/max der Luminanz der deckenden Pixel (Alpha ≥ 128), als RGB565
    - ohne transparente Pixel: 4-Farben-Modus mit `c0 > c1`
    - mit transparenten Pixeln: `c0 <= c1`, Index 3 = transparent, Palette `c0, c1, (c0+c1)/2`
    - Index je Pixel: nächste Palettenfarbe nach quadratischem RGB-Abstand
  - Reihenfolge: 8×8-Kacheln zeilenweise, darin die Unterblöcke links oben, rechts oben, links unten, rechts unten.

- [ ] **Step 4: Run, expect PASS** (alle Host-Tests).

- [ ] **Step 5: Commit** `feat(core): CMPR-Encoder und .tex-Format`

### Task 2: Cover einpassen und Cover-Pfade

**Files:**
- Create: `src/core/cover_image.hpp/.cpp`, `src/core/cover_paths.hpp/.cpp`
- Test: `tests/host/core/cover_image_test.cpp`, `tests/host/core/cover_paths_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `kCoverW`, `kCoverH` (Task 1), `Game`, `consoleInfo` (`core/console.hpp`)
- Produces:
  - `std::vector<u8> fitCover(std::span<const u8> rgba, u32 w, u32 h)`:
    - liefert 200×280 RGBA
    - Seitenverhältnis bleibt, zentriert, freie Ränder Alpha 0
    - Box-Filter beim Verkleinern, bilinear beim Vergrößern
    - leeres Ergebnis bei `w == 0`, `h == 0` oder zu kleinem Puffer
  - `std::string coverStem(const Game&)`: `"<konsole>/<ID6>"` bzw. `"<konsole>/<dateiname ohne letzte Endung>"`, Konsolen-Kürzel aus der Konsolentabelle
  - `std::string coverSrcPath(std::string_view libRoot, std::string_view stem)`: `<lib>/newlauncher/covers_src/<stem>.png`
  - `std::string coverTexPath(std::string_view libRoot, std::string_view stem)`: `<lib>/newlauncher/cache/covers/<stem>.tex`

- [ ] **Step 1: Write the failing tests:**
  - `TEST_CASE("fitCover füllt 5:7 ganz")`:
    - 100×140 deckend rot
    - Ergebnis 200·280·4 Byte
    - Pixel (0,0), (199,279) und (100,140) rot mit Alpha 255
  - `TEST_CASE("fitCover quadratisch: Ränder oben/unten transparent")`:
    - 200×200 deckend
    - Zeilen 0–39 und 240–279 Alpha 0
    - Zeile 140 Alpha 255
  - `TEST_CASE("fitCover breit: Ränder links/rechts")`: 400×100 ergibt 200×50 in der Mitte; Pixel (100,10) Alpha 0, Pixel (100,140) Alpha 255.
  - `TEST_CASE("fitCover extreme Formate")`:
    - 1×1 liefert volle Fläche
    - 10000×10 liefert mindestens eine deckende Zeile, kein Absturz
    - 0×5 liefert ein leeres Ergebnis
  - `TEST_CASE("coverStem")`:
    - Wii `RMCP01` ergibt `"wii/RMCP01"`
    - GC `GM8P01` ergibt `"gc/GM8P01"`
    - `nes/Super Mario Bros. (World).nes` ergibt `"nes/Super Mario Bros. (World)"`
    - `n64/A.B.z64` ergibt `"n64/A.B"`
  - `TEST_CASE("Cover-Pfade")`: `coverSrcPath("sd:", "nes/X")` ergibt `"sd:/newlauncher/covers_src/nes/X.png"`; `coverTexPath` analog mit `cache/covers` und `.tex`.

- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement.** Spiel-Schlüssel nach Spec §5.2; der Stamm entsteht aus dem Teil nach dem ersten `/`, abgeschnitten am letzten `.`.
- [ ] **Step 4: Run, expect PASS.**
- [ ] **Step 5: Commit** `feat(core): Cover auf 200×280 einpassen, Cover-Pfade`

### Task 3: Cover-LRU

**Files:**
- Create: `src/core/cover_cache.hpp/.cpp`
- Test: `tests/host/core/cover_cache_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `struct CoverMemory { virtual void* alloc(u32 bytes) = 0; virtual void free(void* p) = 0; }`:
    - Wii: `memalign(32)` + `free`
    - Host-Test: zählende Fake-Implementierung
  - `enum class CoverState : u8 { Unknown, Pending, Ready, Missing }`
  - `enum class CoverPriority : u8 { Prefetch, Visible }`
  - `class CoverCache`:
    - `CoverCache(u32 budgetBytes, CoverMemory& mem)`
    - `void beginFrame(u32 frame, float nowMs)`
    - `CoverState want(std::string_view stem, CoverPriority)`:
      - neuer Stamm wird `Pending`
      - merkt Frame und höchste Priorität
      - allokiert nur für neue Stämme
    - `std::optional<std::string> nextRequest()`:
      - liefert den `Pending`-Eintrag, der in diesem Frame gewünscht wurde, mit höchster Priorität; bei Gleichstand der zuletzt gewünschte
      - markiert ihn als „in Arbeit“, sodass er nicht doppelt kommt
      - `nullopt`, wenn keiner da ist
    - `void complete(std::string_view stem, void* texels, u32 bytes)`: `texels` stammt aus `mem.alloc`; wird `Ready`, danach wird bis zum Budget verdrängt
    - `void fail(std::string_view stem)`: wird `Missing`
    - `void invalidate(std::string_view stem)`: `Missing` wird zu `Unknown`, damit nach einem Download neu angefragt wird
    - `const void* texels(std::string_view stem) const`
    - `float fadeAlpha(std::string_view stem) const`: 0..1, linear über 150 ms ab `complete`
    - `std::optional<u16> slotOf(std::string_view stem) const`
    - `const void* texelsOfSlot(u16) const`
    - `u32 bytesUsed() const`, `u32 readyCount() const`, `bool idle() const` (keine offene Anfrage)
    - `void clear()`
  - Verdrängung:
    - nur `Ready`-Einträge, die im aktuellen Frame nicht gewünscht wurden
    - der älteste zuletzt gewünschte Frame zuerst
    - Speicher über `mem.free`
  - Ein `Pending`-Eintrag, der seit mehr als 1 Frame nicht gewünscht wurde und noch nicht in Arbeit ist, fällt zurück auf `Unknown`. Schnelles Scrollen staut so keine veralteten Anfragen.

- [ ] **Step 1: Write the failing tests** (Fake-Speicher zählt alloc/free, Budget 3·28 000):
  - `TEST_CASE("want legt Pending an, nextRequest liefert Visible vor Prefetch")`
  - `TEST_CASE("complete → Ready, texels/slotOf gesetzt, fadeAlpha 0 → 0.5 nach 75 ms → 1")`
  - `TEST_CASE("Budget: ältester, nicht gewünschter Eintrag wird verdrängt")`:
    - 4 Cover nacheinander in verschiedenen Frames
    - danach `bytesUsed() <= budget`
    - der erste ist `Unknown` und wurde per `free` freigegeben
  - `TEST_CASE("Gewünschte Cover werden nie verdrängt")`: 4 Cover im selben Frame gewünscht und fertig. Alle 4 bleiben `Ready`, das Budget wird überschritten; im nächsten Frame mit nur 1 gewünschten wird auf das Budget verdrängt.
  - `TEST_CASE("Veraltete Pending-Anfragen verfallen")`: in Frame 1 gewünscht, in Frame 3 nicht mehr. `nextRequest()` liefert `nullopt`, `want` in Frame 4 legt neu an.
  - `TEST_CASE("fail → Missing, invalidate → Unknown → wieder Pending")`
  - `TEST_CASE("In Arbeit wird nicht doppelt geliefert, complete nach Verfall wird trotzdem übernommen")`
  - `TEST_CASE("clear gibt alles frei")`: Zahl der `free` == Zahl der `alloc`.

- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement.**
  - Einträge liegen in einem `std::vector<Entry>` mit festen Slot-Nummern; freie Slots werden wiederverwendet.
  - Suche über eine `std::unordered_map<std::string, u16, Hash, Eq>` mit transparentem Hash (`is_transparent`), damit `want(string_view)` bei bekannten Stämmen nicht allokiert.
  - Ein Ring der Anfrage-Kandidaten ist nicht nötig: `nextRequest` läuft linear über die Slots. Das reicht bei ≤ 640 Einträgen und einem Aufruf pro Frame.

- [ ] **Step 4: Run, expect PASS.**
- [ ] **Step 5: Commit** `feat(core): Cover-LRU mit Budget, Prioritäten und Einblendung`

### Task 4: HTTP und Download-Logik

**Files:**
- Create: `src/core/http.hpp/.cpp`, `src/core/cover_download.hpp/.cpp`
- Test: `tests/host/core/http_test.cpp`, `tests/host/core/cover_download_test.cpp`
- Modify: `CMakeLists.txt`, `src/core/config.hpp/.cpp` (+ `debugCoverUrl`, `debugFetchCovers`), `tests/host/core/config_test.cpp`

**Interfaces:**
- Produces:
  - `struct Url { std::string host; u16 port; std::string path; }`
  - `std::optional<Url> parseUrl(std::string_view)`: nur `http://`, Port optional (Standard 80)
  - `std::string buildGetRequest(const Url&)`: `GET <path> HTTP/1.1\r\nHost: <host>\r\nUser-Agent: newlauncher\r\nConnection: close\r\n\r\n`
  - `class HttpParser`:
    - `explicit HttpParser(u32 maxBody)`
    - `enum class State { Headers, Body, Done, Error }`
    - `State feed(std::span<const u8>)`
    - `State finish()`: Verbindungsende; ohne Längenangabe heißt das `Done`
    - `int status() const`
    - `std::span<const u8> body() const`
  - `std::vector<std::string_view> coverRegions(std::string_view configRegion, std::string_view systemRegion)`
  - `std::string gametdbCoverUrl(std::string_view base, std::string_view region, std::string_view id6)`: `<base>/<REGION>/<ID6>.png`
  - `struct CoverJob { std::string id6; std::string stem; }`
  - `std::vector<CoverJob> coverDownloadJobs(std::span<const Game* const> games, const std::function<bool(const std::string&)>& exists, std::string_view libRoot)`: nur Wii/GC, deren `covers_src`-PNG **und** `.tex` fehlen
  - `bool isPng(std::span<const u8>)`: 8-Byte-Signatur
  - `struct DownloadSummary { u32 ok = 0, missing = 0, failed = 0; }`
  - `Config::debugCoverUrl` (leer = GameTDB) und `bool debugFetchCovers`

- [ ] **Step 1: Write the failing tests:**
  - `TEST_CASE("parseUrl")`:
    - `"http://art.gametdb.com/wii/cover"` ergibt Host `art.gametdb.com`, Port 80, Pfad `/wii/cover`
    - `"http://127.0.0.1:8123/x"` ergibt Port 8123
    - `"https://a"` und `"http://"` ergeben `nullopt`
  - `TEST_CASE("buildGetRequest")`: wörtlich wie oben.
  - `TEST_CASE("HttpParser Content-Length, Kopf in Stücken")`: Antwort byteweise füttern; Status 200, Body exakt.
  - `TEST_CASE("HttpParser chunked")`: `"4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n"` ergibt `"Wikipedia"`, `Done`.
  - `TEST_CASE("HttpParser ohne Länge endet mit finish")`
  - `TEST_CASE("HttpParser 404 und 301")`: Status wird geliefert, der Body ist egal.
  - `TEST_CASE("HttpParser Body über maxBody → Error")`
  - `TEST_CASE("HttpParser Müll → Error")`: `"HELLO\r\n\r\n"`.
  - `TEST_CASE("coverRegions")`:
    - `("auto","DE")` ergibt `DE,EN,US,JA`
    - `("auto","EN")` ergibt `EN,US,JA`
    - `("US","DE")` ergibt `US,EN,JA`
    - `("JA","JA")` ergibt `JA,EN,US`
    - unbekannte Werte wie `("xx","EN")` werden wie `auto` behandelt
  - `TEST_CASE("gametdbCoverUrl")`: `("http://art.gametdb.com/wii/cover","EN","RMCP01")` ergibt `".../wii/cover/EN/RMCP01.png"`.
  - `TEST_CASE("coverDownloadJobs nur Wii/GC ohne PNG und ohne .tex")`: vier Spiele (Wii mit PNG, Wii ohne alles, GC mit `.tex`, NES ohne alles); genau ein Auftrag.
  - `TEST_CASE("isPng")`
  - `config_test`: `[debug] cover_url` und `fetch_covers = 1` werden gelesen; ohne sie gelten leer und `false`.

- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement.**
  - Kopfzeilen: Groß/klein egal, Kopf endet mit `\r\n\r\n`, Kopf höchstens 8 KiB.
  - Status 2xx mit Body ist für den Aufrufer `ok`, 404 ist `missing`, alles andere `fail`. Die Einordnung macht der Netz-Worker (Task 9).

- [ ] **Step 4: Run, expect PASS.**
- [ ] **Step 5: Commit** `feat(core): HTTP-Parser und Logik für den Cover-Download`

### Task 5: `tools/prepare_meta.py`

**Files:**
- Create: `tools/prepare_meta.py`, `tools/tests/test_prepare_meta.py`
- Modify: `.gitignore` (`.cache/`)

**Interfaces:**
- Consumes: `metabin.write_meta_bin`, `metabin.meta_key`
- Produces (CLI): `python tools/prepare_meta.py <lib> [--wiitdb <wiitdb.xml>] [--dat-dir <dir>] [--offline] [--lang DE]`
  - Schreibt `<lib>/newlauncher/cache/meta.bin` und gibt `Einträge: n (Wii/GC m, Retro k)` aus.
  - Funktionen für Tests:
    - `scan_library(lib) -> list[tuple[str, str]]`: Paare (Spiel-Schlüssel, Konsole)
    - `parse_wiitdb(path, ids, lang) -> dict`
    - `parse_dat(text, field) -> dict[str, tuple[str, str]]`: Name → (Wert, CRC)
    - `retro_title(stem) -> str`
    - `build_entries(...)`
    - `fetch_dats(dat_dir, fetch)`: `fetch(url) -> bytes | None` ist injizierbar

- [ ] **Step 1: Write the failing tests** (Bibliothek in `tempfile` über `make_fixtures`-Helfer, kein Netz):
  - `test_scan_library_finds_disc_ids_and_roms`:
    - `wbfs/X [RMCP01]/RMCP01.wbfs` mit Kopf bei 512
    - `games/Y [GM8P01]/game.iso`
    - `roms/nes/A.nes` und `roms/nes/readme.txt`
    - erwartet: Schlüssel `RMCP01`, `GM8P01`, `nes/A.nes`, aber nicht `readme.txt`
  - `test_parse_wiitdb_picks_language_with_fallback`: XML mit zwei Spielen, eines nur `EN`; `--lang DE` liefert DE-Titel bzw. den EN-Rückfall, dazu Jahr, Spieler und Synopsis.
  - `test_parse_dat_reads_comment_value_and_crc`: Ausschnitt im echten Format (`game ( comment "…" releaseyear "1998" rom ( crc B0256101 ) )`).
  - `test_retro_title_strips_tags`: `"Super Mario 64 (Europe) (En,Fr,De)"` ergibt `"Super Mario 64"`; `"Zelda [!]"` ergibt `"Zelda"`.
  - `test_build_entries_matches_by_name_then_crc`: eine ROM per Name, eine per CRC32 des Dateiinhalts, eine ohne Treffer (Jahr 0, Spieler 0, Titel aus dem Namen).
  - `test_main_writes_meta_bin`: `read_meta_bin` enthält die erwarteten Schlüssel (`meta_key`).
  - `test_fetch_dats_skips_existing_and_offline`

- [ ] **Step 2: Run, expect FAIL.**
  Run: `python -m unittest discover -s tools/tests`
- [ ] **Step 3: Implement.**
  - Systemnamen:
    - `nes` → „Nintendo - Nintendo Entertainment System“
    - `snes` → „Nintendo - Super Nintendo Entertainment System“
    - `gb` → „Nintendo - Game Boy“
    - `gbc` → „Nintendo - Game Boy Color“
    - `gba` → „Nintendo - Game Boy Advance“
    - `n64` → „Nintendo - Nintendo 64“
  - Quelle: `https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/{releaseyear,maxusers}/<System>.dat`
  - `wiitdb.xml` per `iterparse`, nur gesuchte IDs behalten.
  - Standard für `--dat-dir`: `<repo>/.cache/libretro`.

- [ ] **Step 4: Run, expect PASS.**
- [ ] **Step 5: Commit** `feat(tools): prepare_meta.py erzeugt meta.bin aus GameTDB und libretro`

### Task 6: `tools/prepare_covers.py` und PNG-Schreiber

**Files:**
- Create: `tools/pngfile.py`, `tools/prepare_covers.py`, `tools/tests/test_prepare_covers.py`, `tools/tests/test_pngfile.py`

**Interfaces:**
- Produces:
  - `pngfile.write_png(path, w, h, rgba: bytes)`
  - `pngfile.solid_png(w, h, rgba_tuple) -> bytes`: RGBA8, Filter 0, `zlib`
  - `prepare_covers.thumbnail_name(stem) -> str`: ersetzt `&*/:\`<>?\|"` durch `_`
  - `prepare_covers.thumbnail_url(console, stem) -> str`: `https://thumbnails.libretro.com/<System>/Named_Boxarts/<quote(name)>.png`
  - `prepare_covers.main(lib, force=False, fetch=...)`:
    - legt `covers_src/<konsole>/<stamm>.png` an
    - überspringt vorhandene Dateien ohne `--force`
    - zählt `ok`, `vorhanden`, `fehlt` (404 oder keine PNG)

- [ ] **Step 1: Write the failing tests:**
  - `test_solid_png_is_valid`: Signatur, IHDR 4×2 Farbtyp 6, `zlib.decompress` der IDAT ergibt 2·(1+16) Byte.
  - `test_thumbnail_name_replaces_reserved`: `"Tom & Jerry: A/B"` ergibt `"Tom _ Jerry_ A_B"`.
  - `test_thumbnail_url_quotes`: N64-Beispiel aus der Prüfung oben (`Super%20Mario%2064%20%28Europe%29...` bzw. so wie `urllib.parse.quote` es liefert, mit `safe=""` für den Namen).
  - `test_main_downloads_skips_and_counts`: Fake-`fetch` liefert PNG, `None` bzw. HTML; geprüft werden Dateien und Zählung; ein zweiter Lauf überspringt.

- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement** (`urllib.request` mit Timeout 20 s als Standard-`fetch`; 404 → `None`).
- [ ] **Step 4: Run, expect PASS.**
- [ ] **Step 5: Commit** `feat(tools): prepare_covers.py lädt Retro-Boxarts von libretro-thumbnails`

### Task 7: Cover in der UI (host-getestet)

**Files:**
- Create: `src/ui/cover_provider.hpp`
- Modify:
  - `src/gfx/draw_list.hpp`: `using TextureId = u16`; `inline constexpr TextureId kCoverTextureBase = 1`, Cover-Slot `s` ist `kCoverTextureBase + s`
  - `src/ui/main_scene.*`, `src/ui/details_scene.*`
- Test: `tests/host/ui/main_scene_test.cpp`, `tests/host/ui/details_scene_test.cpp` (+ `tests/host/support/fake_covers.hpp`)

**Interfaces:**
- Produces:
  - `struct CoverView { gfx::TextureId texture = 0; float alpha = 0; }`: `texture == 0` heißt Platzhalter
  - `class CoverProvider`:
    - `virtual void want(const Game&, CoverPriority) = 0`
    - `virtual CoverView view(const Game&) const = 0`
  - `MainScene::setCovers(CoverProvider*)` und `DetailsScene::setCovers(CoverProvider*)`; `nullptr` heißt immer Platzhalter, so wie heute

- [ ] **Step 1: Write the failing tests:**
  - `main_scene_test` „fordert sichtbare und zwei Reihen voraus an“:
    - Fake zählt `want` pro Spiel und Priorität
    - Bibliothek mit 60 Spielen in einer Konsole
    - nach `update`: alle sichtbaren Kacheln `Visible`, die zwei Reihen darunter `Prefetch`, Kachel 59 gar nicht
  - „zeichnet Cover-Quad mit Textur und Alpha“: Der Fake meldet Textur 7 und Alpha 0.5 für Spiel 0. Die DrawList enthält einen Batch mit Textur 7, dessen Vertexfarbe Alpha 127 oder 128 hat.
  - „ohne Cover bleibt der Platzhalter“: kein Batch mit Textur ≠ 0.
  - `details_scene_test`: Das große Cover nutzt `view()`; `want` erfolgt mit `Visible`.

- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement.**
  - Das Cover wird in die Kachel eingepasst: 4 px Rand, Seitenverhältnis 5:7, zentriert, mit UV 0..1.
  - Der Konsolen-Tag und der Favoritenpunkt liegen darüber. Ohne Cover bleibt die heutige Kachel aus Tag und Titel als Platzhalter.
  - `want` wird in `update` aufgerufen, nicht in `draw`.

- [ ] **Step 4: Run, expect PASS.**
- [ ] **Step 5: Commit** `feat(ui): Cover im Raster und in der Detailansicht`

### Task 8: Cover auf der Wii: PNG, Cover-Thread, Renderer, Fixtures

**Files:**
- Create:
  - `src/gfx/texture_table.hpp`: `struct CoverTextures { virtual const void* texels(TextureId) const = 0; }`
  - `src/platform/png_decode.hpp/.cpp`
  - `src/platform/cover_worker.hpp/.cpp`
- Modify:
  - `src/gfx/renderer.hpp/.cpp`: `submit(const DrawList&, const CoverTextures* covers)`
  - `src/app/main.cpp`
  - `CMakeLists.txt`: Wii-Quellen; `png` und `z` linken, wenn nicht schon über FreeType
  - `.github/workflows/build.yml`: `ppc-libpng`
  - `tools/make_fixtures.py`, `tools/deploy_dolphin.py` + Tests

**Interfaces:**
- Consumes:
  - `encodeCmpr`, `writeTex`, `parseTex` (Task 1)
  - `fitCover`, `coverStem`, Pfade (Task 2)
  - `CoverCache`, `CoverMemory` (Task 3)
  - `CoverProvider` (Task 7)
- Produces:
  - `Result<std::vector<u8>> platform::decodePng(std::span<const u8>, u32& w, u32& h)`:
    - RGBA8
    - Palette, Graustufen, 16 Bit und Interlace werden über `png_set_*` umgewandelt
    - Abbruch über `setjmp`, ohne Exceptions
    - Pixelgrenze 4096×4096
  - `class platform::CoverWorker`, gleiches Muster wie `ScanWorker`:
    - `bool start(FileSystem&, std::string libRoot, std::string stem, CoverMemory&)`
    - `std::optional<CoverResult> take()`
    - `CoverResult { std::string stem; void* texels; u32 bytes; bool converted; std::string error; }`
  - Ablauf im Thread:
    1. `.tex` lesen und prüfen; passt sie, wird sie kopiert und fertig.
    2. Sonst die PNG lesen (höchstens 4 MiB), `decodePng`, `fitCover`, `encodeCmpr`, `writeTex`.
    3. Mit `fs.makeDirs` die `.tex` atomar schreiben.
    4. Texel in `mem.alloc(28000)` kopieren und `DCFlushRange`.
  - In `main`:
    - `class CoverService : ui::CoverProvider, gfx::CoverTextures` verbindet `CoverCache` und Worker
    - pro Frame: höchstens eine Anfrage an den freien Worker, Ergebnisse übernehmen, Marker loggen
    - einmalig `NL_COVERS ready=<n>`, wenn nach `NL_READY` `idle()`
    - Bibliothekswechsel: Cache bleibt, die Schlüssel sind Stämme
  - Renderer:
    - Batch mit Textur ≥ 1: CMPR-`GXTexObj` 200×280, `GX_CLAMP`, linear
    - TEV `GX_MODULATE`; zurück zum Atlas mit der bisherigen TEV-Stufe
    - `GX_InvalidateTexAll()` einmal pro Frame, wenn sich die Slots geändert haben (Zähler im Cache)
    - Speicher erst freigeben, nachdem `endFrame` `GX_DrawDone` abgewartet hat; das ergibt sich aus der Reihenfolge Update → Draw → `endFrame`
  - Debug-Overlay: Texturspeicher = Atlas + `bytesUsed()`.
  - Fixtures (`make_fixtures.py`, mit `pngfile.solid_png`):
    - `newlauncher/covers_src/nes/Super Mario Bros. (World).png` (128×180 rot)
    - `newlauncher/covers_src/n64/Mario Kart 64 (Europe).png` (300×300 blau, quadratisch)
    - `newlauncher/covers_src/gb/Tetris (World) (Rev 1).png`: 40 Byte Müll, für `NL_COVER_FAIL`
  - `deploy_dolphin.deploy(..., fixtures_dir)`: löscht vor dem Kopieren `newlauncher/cache/covers` und `newlauncher/covers_src` im SD-Ordner, damit jede Abnahme wirklich konvertiert und lädt.

- [ ] **Step 1: Write the failing tests** (Python):
  - `test_make_fixtures` prüft die drei Cover-Dateien.
  - `test_deploy_clears_cover_cache_with_fixtures`, und ohne Fixtures bleibt alles stehen.
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement** Python-Teil, dann den Wii-Teil wie oben.
- [ ] **Step 4: Run:**
  - Tool-Tests PASS
  - `python tools/build_wii.py` ohne Warnungen
  - `python tools/make_fixtures.py tests/fixtures/sd`
  - Smoke-Test:
    `python tools/dolphin_smoke.py --marker "NL_COVER_CONVERTED nes/Super Mario Bros. (World)" --marker "NL_COVER_CONVERTED n64/Mario Kart 64 (Europe)" --marker "NL_COVER_FAIL gb/Tetris (World) (Rev 1)" --marker "NL_COVERS ready="`
  - Expected: alle Marker. Ein Screenshot über das Smoke-Skript bzw. Dolphin zeigt Cover im Raster (NES-Leiste).
- [ ] **Step 5: Commit** `feat: Cover-Thread, PNG→CMPR auf der Wii und Cover im Renderer`

### Task 9: Netz und „Fehlende Cover laden“

**Files:**
- Create:
  - `src/platform/http_client.hpp/.cpp`
  - `src/platform/net_worker.hpp/.cpp`
  - `src/ui/actions_scene.hpp/.cpp`
  - Test: `tests/host/ui/actions_scene_test.cpp`
- Modify:
  - `src/ui/home_menu.*`: Kachel „Einstellungen“ ergibt `out.pop` + `out.push(actions)` über `HomeMenu::setActions(Scene*)`
  - `src/ui/scene.hpp`: `AppCommand::FetchCovers`, Name `fetch_covers`
  - `src/ui/strings.hpp`
  - `src/app/main.cpp`
  - `tools/dolphin_smoke.py` (+ Tests)
  - `tests/host/ui/home_menu_test.cpp`

**Interfaces:**
- Consumes: `parseUrl`, `buildGetRequest`, `HttpParser`, `coverRegions`, `gametdbCoverUrl`, `coverDownloadJobs`, `isPng`, `DownloadSummary` (Task 4)
- Produces:
  - `enum class HttpOutcome : u8 { Ok, NotFound, Failed }`
  - `HttpOutcome platform::httpGet(const Url&, u32 maxBody, std::vector<u8>& body)`:
    - DNS über `net_gethostbyname`; IP-Literale direkt
    - Sockets mit 10 s Timeout über `net_poll`
  - `class platform::NetWorker`:
    - `bool start(FileSystem&, std::string libRoot, std::vector<CoverJob>, std::vector<std::string> regions, std::string baseUrl)`
    - `u32 done() const`, `u32 total() const`, `bool running() const`
    - `std::optional<DownloadSummary> take()`
    - `std::vector<std::string> takeFinishedStems()`: für `CoverCache::invalidate`
  - Ablauf im Thread:
    1. `net_init()` mit bis zu 3 Versuchen; Ergebnis `NL_NET ok` bzw. `NL_NET_FAIL <code>`, dann zählen alle Aufträge als `failed`.
    2. Pro Auftrag die Regionen der Reihe nach: 200 + PNG → atomar unter `covers_src/<stamm>.png` speichern, `ok`, danach die nächsten Aufträge; 404 oder keine PNG → nächste Region; nach allen Regionen `missing`; Fehler → `failed`.
    3. Je Auftrag ein Marker `NL_COVER_DL`.
  - `ActionsScene`: Overlay-Liste mit zwei Einträgen, Steuerkreuz und Zeiger, A bestätigt, B schließt:
    - „VC-Injects zuordnen“ → `OpenVcMap` + `pop`
    - „Fehlende Cover laden“ → `FetchCovers` + `pop`
  - In `main`:
    - `FetchCovers` startet den Worker, sofern er nicht schon läuft; ohne Aufträge sofort ein Dialog „Keine fehlenden Cover“.
    - Während des Laufs zeigt der Toast `Cover: <done>/<total>` (Text nur bei Änderung neu bauen).
    - Am Ende: `NL_COVER_DL_DONE …` und ein Dialog „Cover geladen“ mit `<ok> geladen, <missing> nicht gefunden, <failed> fehlgeschlagen`.
    - `[debug] fetch_covers = 1` löst nach `NL_READY` einmal `FetchCovers` aus.
  - `systemRegion`: `CONF_GetLanguage()` Deutsch → `DE`, Französisch → `FR`, Japanisch → `JA`, sonst `EN`.
  - `dolphin_smoke.py --cover-server`:
    - startet `http.server` in einem Thread auf `127.0.0.1:<freier Port>`
    - dient ein Temp-Verzeichnis mit `EN/RMCP01.png` und `US/GM8P01.png` aus (per `pngfile`)
    - schreibt `[debug] cover_url = http://127.0.0.1:<port>` und `fetch_covers = 1` in die `config.ini` im SD-Ordner, wie es `--autostart` heute tut

- [ ] **Step 1: Write the failing tests:**
  - `actions_scene_test`: zwei Einträge; A auf Eintrag 1 ergibt `FetchCovers` + `pop`; B ergibt nur `pop`.
  - `home_menu_test`: Die Kachel „Einstellungen“ ergibt `pop` + `push` der Aktionsliste, sonst keinen Befehl.
  - `test_dolphin`: `--cover-server` schreibt beide Debug-Schlüssel und legt die PNGs an der richtigen Stelle ab (Server-Start gemockt).
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement** wie oben.
- [ ] **Step 4: Run:**
  - Host- und Tool-Tests PASS
  - `python tools/build_wii.py` ohne Warnungen
  - `python tools/dolphin_smoke.py --cover-server --marker "NL_NET ok" --marker "NL_COVER_DL RMCP01 EN ok" --marker "NL_COVER_DL GM8P01 US ok" --marker "NL_COVER_DL RMGP01" --marker "NL_COVER_DL_DONE ok=2 missing=1 failed=0"`
  - Expected: alle Marker. RMGP01 hat kein Bild auf dem Server, also `missing`.
  - **Rückfall:** Erreicht Dolphin `127.0.0.1` nicht, zuerst die LAN-IP des PCs probieren. Geht beides nicht, als Ruling ledgern: Download-Abnahme erst in M7. Der Parser ist host-getestet; der Rest von Task 9 bleibt drin.
- [ ] **Step 5: Commit** `feat: „Fehlende Cover laden“ über GameTDB mit Netz-Thread`

### Task 10: Selbsttest, Abnahme und README

**Files:**
- Modify: `src/app/selftest.hpp/.cpp`, `src/app/main.cpp`, `README.md`

- [ ] **Step 1: Selbsttest anpassen:**
  - HOME → „Einstellungen“ öffnet jetzt die Aktionsliste. Nach dem bisherigen A in Frame 334 kommt ein A in Frame 350 auf „VC-Injects zuordnen“; alle späteren Frames verschieben sich um 16.
  - Prüfen, dass das Leerlauf-Fenster (`kIdleStart`/`kIdleEnd`) danach noch hinter allen Eingaben liegt; sonst beide um denselben Betrag verschieben.
  - Neu in `SelftestView`: `const CoverCache* covers`.
  - Vor dem Leerlauf-Fenster prüfen: `covers->readyCount() >= 1` und `covers->idle()`.
- [ ] **Step 2: Abnahme:**
  1. Host-Tests
  2. Tool-Tests
  3. Wii-Build ohne Warnungen
  4. Smoke-Test Cover (Task 8)
  5. Smoke-Test Download (Task 9)
  6. Selbsttest mit allen bisherigen Markern plus `NL_ALLOC_IDLE 0` (Befehl aus dem README)
  7. `prepare_meta.py` und `prepare_covers.py` einmal mit `--help` aufrufen
- [ ] **Step 3: README:**
  - Abschnitt „Cover und Metadaten“ mit beiden Skripten: Beispielaufrufe, woher `wiitdb.xml` kommt, `--lang`
  - Aktion „Fehlende Cover laden“
  - Hinweis: `cache/covers` löschen, um neu zu konvertieren
  - neue Marker und Debug-Schlüssel, Smoke-Befehle
- [ ] **Step 4: Commit** `docs: M5 im README, Selbsttest mit Covern`
