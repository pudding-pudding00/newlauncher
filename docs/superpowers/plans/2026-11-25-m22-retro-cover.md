# M22 Retro-Cover Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Der Launcher lädt fehlende Retro-Boxarts von libretro-thumbnails (GitHub) und merkt sich Fehlschläge.

**Architecture:** `CoverJob` bekommt eine fertige `url`. Ist sie gesetzt, versucht `runCoverDownloads` nur diese
URL statt der GameTDB-Regionen. `coverDownloadJobs` erzeugt Retro-Jobs, wenn eine Retro-Basis übergeben wird. Eine
reine Funktion führt die Fehlliste fort, der Netz-Thread schreibt sie je Wurzel.

**Spec:** docs/superpowers/specs/2026-11-25-m22-retro-cover-design.md

## Global Constraints

- `src/core/` ohne `<ogc/...>`. Kein throw, kein RTTI. 0 Warnungen mit und ohne TLS.
- Kein Rückfall von HTTPS auf HTTP; ohne TLS ist `retroCoverBase` leer.
- UI-Texte DE+EN (hier keine neuen).

## Review Focus

- Dateinamen mit `'`, `,`, `&`, Umlauten → korrekt kodierte URL.
- Fehlliste: Lauf von Hand entfernt wiedergefundene Stämme; automatischer Lauf behält übersprungene.
- Smokes dürfen nie ins Internet (Retro-Basis im Smoke immer lokal).
- Bestehende Wii/GC-Abläufe (Regionen, Abbruch nach 3 Fehlern) unverändert.
- Keine Doppelten: Stamm gleich bei ROMs mit gleichem Namen auf verschiedenen Geräten (je Wurzel getrennt).

### Task 1: core — URL, Jobs, Fehlliste

**Files:** `src/core/cover_download.{hpp,cpp}`, `tests/host/core/cover_download_test.cpp`

- `std::string libretroCoverUrl(std::string_view base, Console c, std::string_view fileStem)`; leer für Konsolen
  ohne libretro-System oder leere Basis.
- `CoverJob::url` (leer = GameTDB über Regionen).
- `coverDownloadJobs(games, exists, libRoot, retroBase, skip)`: Retro-Jobs nur mit `retroBase`; `skip` = Stämme
  aus der Fehlliste.
- `existingCoverFiles` listet alle Konsolenordner (nicht nur wii/gc).
- `runCoverDownloads`: Job mit `url` → genau eine Anfrage; `CoverDownloadIo::missed(stem)` bei missing.
- `std::vector<std::string> parseMissingList(std::string_view)`, `std::string missingListText(...)`,
  `updateMissingList(old, jobStems, missedStems)`: Ergebnis = (old − jobStems) ∪ missed, sortiert, ohne Doppelte.
- Tests zuerst; Host-Suite grün.

### Task 2: Config, Netz-Thread, main

- `Config::retroCoverBase` aus `[covers] retro_base` (Standard `kLibretroCoverBase` mit TLS, sonst leer), in
  `defaultConfigText` kommentiert. Test in `config_test`.
- `fetchCovers`: Fehlliste je Wurzel lesen; bei `silent` als `skip`. `debugCoverUrl` ersetzt auch die Retro-Basis.
- `NetWorker`: gesammelte missed-Stämme je Wurzel → `updateMissingList` → `writeAtomic` nach
  `<wurzel>/newlauncher/cache/covers/missing.txt`.

### Task 3: Smoke und PC-Skript

- `dolphin_smoke.py`: Cover-Server liefert zusätzlich ein Retro-PNG für eine Fixture-ROM ohne Cover; Marker der
  Cover-Smokes anpassen. Zweiter Lauf `--cover-auto` mit `--no-fixtures`: Fehlliste wirkt (weniger Jobs).
- `prepare_covers.py`: neue Quelle; Tool-Tests anpassen.

### Task 4: Doku, Builds, Smokes, PR

- STATUS.md, Abnahmeliste `docs/acceptance/m22-wii.md`; beide Wii-Builds 0 Warnungen; Smokes; PR, CI, Merge.
