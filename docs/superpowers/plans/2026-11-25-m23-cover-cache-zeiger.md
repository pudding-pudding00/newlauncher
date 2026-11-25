# M23: Cover-Cache löschen, Zeiger-Scrollen, Kacheln im Cover-Format – Plan

> Spec: `docs/superpowers/specs/2026-11-25-m23-cover-cache-zeiger-design.md`. Inline umgesetzt (TDD je Aufgabe).

## Globale Randbedingungen

UI-Texte DE/EN; `src/core` ohne `<ogc/…>`; kein throw/RTTI; 0 Warnungen mit TLS und `--no-tls`;
`wtls_*` nur im NetService-Thread.

## Aufgaben

1. **Messung** (`src/platform/http_client.*`, `net_worker.cpp`): `lastRequestReused()`, `lastHandshakeMs()`;
   bei `[debug] perf` `NL_PERF cover get … reuse=` und `NL_PERF cover save ms=`.
2. **`clearCoverCache(fs, libRoot)`** (`src/core/cover_download.*`): löscht `covers_src/<k>/*`,
   `cache/covers/<k>/*` (alle Konsolen außer Kanälen) und `missing.txt`; Tests in `cover_download_test.cpp`.
3. **Einstellungszeile** `SettingRow::ClearCovers` nach `FetchCovers`; `Str::ClearCovers`, `Str::ClearCoversConfirm`;
   `AppCommand::ClearCovers` („clear_covers“). Erstes A schärft (`armed_`), zweites A meldet den Befehl; Fokuswechsel,
   B und `onEnter` entschärfen. Karte: vertikaler Rand 8 px (`kPadY`), damit 26 Zeilen in 480 passen.
   main: `net.join()`, `covers.clearAll()`, `clearCoverCache` je Cover-Wurzel, Log `NL_COVERS_CLEARED files= failed=`.
4. **Kachel-Format**: `coverAspect(Console)` (`core/console.hpp`), `gridMetrics(width, aspect)`,
   `fitAspectRect`, `cmprOpaqueBox` (`core/cmpr.*`), `CoverCache::complete(…, box)`/`contentBox`,
   `CoverView{uv, aspect}`; MainScene wählt das Format nach der Konsolenliste, DetailsScene passt ein.
5. **Zeiger-Randscrollen**: `GridModel::scrollBy`, `maxScroll`, `setFocus(i, scrollIntoView)`; MainScene scrollt in
   40-px-Streifen mit bis zu einer Reihe je 250 ms, der Fokus folgt der Kachel ohne Nachscrollen.
6. **Doku und Prüfung**: STATUS, `docs/acceptance/m23-wii.md`; Host-, Tool-Tests, Wii-Builds, Smokes.

## Review-Schwerpunkte

- Löschen während eines laufenden Downloads oder einer Umwandlung (Join-Reihenfolge, keine halben Dateien).
- Mehrere Geräte mit derselben Cover-Wurzel (doppelter Durchlauf darf nichts kaputt machen).
- Cover mit eigenem Alphakanal (transparente PNG-Ränder) verkleinern die Box: erwünscht, nie verzerrt.
- Randscrollen bei weniger Spielen als eine Seite (maxScroll 0) und bei Zeigerstart im Randstreifen.
