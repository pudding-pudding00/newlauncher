# M24 Raster, Cover-Format überall, Cover-Speicher, Löschen ohne Hänger – Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Die acht Wünsche aus der M24-Spec umsetzen.

**Architecture:** Reine Logik (Job-Reihenfolge, Cover-Wurzeln, Reihenhöhen, Halte-Fortschritt) liegt testbar in
`src/core` bzw. `src/ui`; `main.cpp` verdrahtet sie. Das Löschen läuft in einem neuen LWP-Thread
(`platform/cover_clear_worker`), der Netz-Abbruch wird nicht mehr blockierend abgewartet.

**Tech Stack:** C++20, devkitPPC/libogc, doctest (Host), Dolphin-Smokes.

**Spec:** `docs/superpowers/specs/2026-11-25-m24-raster-cover-speicher-design.md`

## Global Constraints
- UI-Texte DE und EN; `src/core` ohne `<ogc/…>`; kein throw/RTTI.
- 0 Warnungen: `python tools/build_wii.py` und `--no-tls`.
- `wtls_*` nur im NetService-Thread; kein Rückfall auf HTTP.
- Maße: 854 {tileW 128, tileH 179, gap 4, marginX 33, topBarH 48, infoBarH 60, 6}; 640 {99, 139, 4, 13, 48, 60, 6};
  `kGridTopPad` 4, `kCoverInset` 2.

## Review Focus
1. Gemischte Liste mit nur einer Reihe bzw. letzter unvollständiger Reihe: Reihenhöhe = höchste vorhandene Kachel.
2. Speicher „SD“ ohne beschreibbare SD: Rückfall auf Gerätewurzeln, kein Schreiben ins Leere.
3. Doppelte Wurzeln (alle Geräte → `sd:`): Löschen und Download je Wurzel genau einmal.
4. Halten: A schon gedrückt beim Fokuswechsel auf die Zeile löst nicht aus (Start nur mit frischem Druck).
5. Löschen während laufendem Download: kein Neustart von Downloads/Cover-Laden, bis das Löschen fertig ist.

---

### Task 1: Download-Reihenfolge nach Generation
**Files:** `src/core/cover_download.{hpp,cpp}`, `tests/host/core/cover_download_test.cpp`, `src/app/main.cpp`
- Produces: `CoverJob::console` (Feld, Standard `Console::Wii`), `int coverJobRank(Console)` (Wii 0, GC 1, GBA 2,
  N64 3, GBC 4, SNES 5, GB 6, NES 7, sonst 8), `void orderCoverJobs(std::vector<CoverJob>&)` (stabil).
- [ ] Test `orderCoverJobs: Generation absteigend, innerhalb stabil` (NES,Wii,SNES,GC,Wii → Wii,Wii,GC,SNES,NES), FAIL.
- [ ] Umsetzen; `coverDownloadJobs` setzt `console`; `fetchCovers` sammelt Spiele je **eindeutiger** Wurzel und ruft
      am Ende `orderCoverJobs(jobs)`. Test PASS.

### Task 2: Cover-Speicher (Config, Wurzeln, Einstellung)
**Files:** `src/core/config.{hpp,cpp}`, `src/core/library_merge.{hpp,cpp}`, `src/core/settings_model.{hpp,cpp}`,
`src/ui/settings_scene.cpp`, `src/ui/strings.{hpp,cpp}`, Tests `config_test`, `library_merge_test`, `settings_model_test`.
- Produces: `enum class CoverStorage : u8 { Sd, Device }`, `Config::coverStorage = Sd`, `[covers] storage = sd|device`;
  `std::vector<std::string> coverRootsFor(std::span<const std::string> devices, std::span<const bool> writable,
  std::string_view dataDir, CoverStorage s, bool sdWritable)`; `std::vector<std::string> uniqueRoots(std::span<const std::string>)`;
  `SettingRow::CoverStorage` (nach CoverAuto), Strings `CoverStorage`, `CoverStorageSd`, `CoverStorageDevice`.
- [ ] Tests: Ini lesen/schreiben (Standard sd, unbekannt → sd + badKeys `covers.storage`), `coverRootsFor` (Sd+sdWritable →
      alle `sd:`; Sd ohne SD → wie coverRoot; Device → coverRoot), `uniqueRoots` (Reihenfolge, ohne Doppelte),
      `stepSetting` schaltet um. FAIL, umsetzen, PASS.
- [ ] `main.cpp`: `coverRootsOf` nutzt `coverRootsFor`; nach SaveSettings mit geändertem Speicher `covers.clearAll()`,
      `covers.setRoots(...)`; SD nicht beschreibbar → `NL_WARN`.

### Task 3: Kompaktes Raster und Reihenhöhen je Reihe
**Files:** `src/ui/layout.{hpp,cpp}`, `src/ui/main_scene.cpp`, `src/ui/jump_bar.cpp`, `tests/host/ui/layout_test.cpp`, `main_scene_test.cpp`
- Produces: `GridModel::reset(int n, const GridMetrics&, float viewH, std::span<const float> itemHeights = {})`;
  `float GridModel::rowHeight(int row) const`.
- [ ] Tests: neue Maße (854 → 128/179, 640 → 99/139, Breite exakt), Aspekt 1 → 128, 10:7 → 90;
      Reihenhöhe = Max der Reihe, kürzere Kachel vertikal zentriert (`tileRect`), `hitTest` in der Lücke über einer
      kurzen Kachel → -1, `maxScroll` aus Summe der Reihenhöhen, `setFocus` macht hohe Reihe ganz sichtbar. FAIL.
- [ ] Umsetzen: Zeilenanfänge vorberechnen; MainScene übergibt in gemischten Listen je Spiel
      `gridMetrics(w, coverAspect(console)).tileH`; Leisten und Infoleiste nutzen `topBarH`/`infoBarH` statt 56/72;
      JumpBar nutzt 60. PASS, bestehende Tests angepasst.

### Task 4: Plakette blendet aus; Sortier-Umschaltung in der A–Z-Leiste
**Files:** `src/ui/main_scene.cpp`, `src/ui/jump_bar.{hpp,cpp}`, `src/ui/scene.hpp` (AppCommand::ToggleSort),
`src/ui/strings.*`, `src/app/main.cpp`, Tests `jump_bar_test`, `main_scene_test`.
- Produces: `JumpTarget::sortLabel() const` (Standard leer = keine Plakette), `JumpBar::sortRect(ctx)`,
  `AppCommand::ToggleSort`, `SortOrder toggledSort(SortOrder)` in `play_stats` (Title↔Year, sonst Title).
- [ ] Tests: − (PrevConsole) in der Leiste → `out.command == ToggleSort`, Leiste bleibt offen; A über `sortRect` dito;
      `toggledSort`. Plakette: Alpha des Tags = 1 − Cover-Alpha (Hilfsfunktion `tagAlpha(bool disc, float coverAlpha)`).
- [ ] main.cpp: ToggleSort → `config.sortOrder = toggledSort(...)`, speichern wie SaveSettings, `mainScene.setSort`, Toast.

### Task 5: Halten zum Bestätigen
**Files:** `src/ui/input.{hpp,cpp}`, `src/ui/settings_scene.{hpp,cpp}`, `src/ui/strings.*`, Tests `input_test`, `settings_scene_test`.
- Produces: `InputFrame::confirmHeld`; `kHoldConfirmMs = 1000`; `SettingsScene::holdProgress() const` (0..1).
- [ ] Tests: A gehalten 1 s auf ClearCovers → ClearCovers (nach SaveSettings wie bisher); 0,5 s und loslassen → nichts;
      Fokuswechsel bricht ab; A schon gehalten beim Fokuswechsel → kein Start. FAIL, umsetzen, PASS.
- [ ] Remote/Smoke: `[debug]`-Weg prüfen und, falls nötig, den Smoke-Ablauf anpassen.

### Task 6: Löschen im Hintergrund, Netz-Abbruch ohne Warten
**Files:** `src/platform/cover_clear_worker.{hpp,cpp}` (neu), `src/platform/net_worker.{hpp,cpp}` (`cancel()`),
`src/platform/http_client.cpp` (`abort_cb`), `src/core/cover_download.{hpp,cpp}` (Fortschritt), `src/app/main.cpp`, Strings.
- Produces: `clearCoverCache(fs, root, const std::function<void()>& onRemoved = {})`; `CoverClearWorker::start(fs, roots)`,
  `removed()`, `running()`, `take() -> std::optional<CoverClearResult>`.
- [ ] Test: `clearCoverCache` ruft onRemoved je gelöschter Datei. FAIL, PASS.
- [ ] main.cpp: Zustand Idle → WaitNet (net.cancel) → Deleting (covers.clearAll, Worker) → Idle; `covers.pump()` und
      `fetchCovers` nur in Idle; Toast „Cover werden gelöscht … n“; Ende `NL_COVERS_CLEARED files= failed= ms=`.
- [ ] wii_tls: `tlsOpt.abort_cb/abort_ctx` aus dem cancel-Zeiger (Handshake bricht ≤ 100 ms ab).

### Task 7: Builds, Smokes, Doku, PR
- [ ] Host-Tests, Tool-Tests, beide Wii-Builds 0 Warnungen, `check_licenses`.
- [ ] Smokes: TLS (`NL_TLS_CA ok`, `NL_COVER_DL_DONE`), Wrong-CA, Remote, return-stub, fresh-data, No-TLS.
- [ ] `docs/acceptance/m24-wii.md`, STATUS (wii_tls 1.0.0 → #39, M24-Zeile), PR, CI, Merge.
