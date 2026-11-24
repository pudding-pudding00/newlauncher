# Hinweise für KI-Agenten (alle Harnesses)

Diese Datei gilt für jedes Agenten-Werkzeug (Claude Code, Codex, Cursor, …). Den aktuellen Projektstand und
die offenen Aufgaben findest du in [docs/STATUS.md](docs/STATUS.md). Lies sie zuerst.

## Kommunikation

- Antworte auf Deutsch.
- UI-Texte gibt es immer auf Deutsch **und** Englisch.

## Arbeitsweise

- Spec → Plan → Umsetzung. Die Specs liegen unter `docs/superpowers/specs/`, die Pläne unter `docs/superpowers/plans/`. Die Spec ist bindend.
- Jeder Meilenstein liefert drei Dinge: Host-Tests für alles in `core/`, Dolphin-Smokes (wo Dolphin das Verhalten zeigt) und eine Hardware-Liste unter `docs/acceptance/mNN-wii.md`.
- TDD: erst einen Test schreiben, der fehlschlägt, dann den Code.
- Arbeite selbstständig weiter. Nach einem fertigen Schritt nicht auf Bestätigung warten.
- PRs selbst mergen, sobald die CI grün ist und die Prüfung gemacht ist: `gh pr merge N --merge`. Kein Auto-Merge, kein Force-Push. Arbeit läuft auf einem Feature-Branch, nie direkt auf `main`.
- Die CI nicht in Schleifen abfragen. Einmal blockierend warten mit `gh run watch <id> --exit-status`.

## Harte Regeln

- **Spieldateien:**
  - Niemals ROMs, ISOs oder WADs einchecken.
  - Keine fremden Banner-Dateien ins Repo.
  - Kein Common Key im Quellcode.
- **Dolphin:**
  - Nur mit `-u` und dem Dev-User `../wii_tools/dolphin-user`.
  - Die private Dolphin-Konfiguration nie anfassen.
  - Keine breiten Prozess-Kills.
- **Echte Wii:**
  - Vor jedem Kopieren auf die SD-Karte den Nutzer fragen.
  - Kein Softmod ohne bekannte System-Menü-Version und MAC-Adresse.
- **Privatsphäre:**
  - Die E-Mail-Adresse des Nutzers bleibt privat.
  - Die PIN der Kindersicherung wird nie geloggt.
  - Der Launcher schreibt nie in SYSCONF.
- **wii_tls** (`third_party/wii_tls`):
  - `wtls_*`-Aufrufe laufen nur im NetService-Thread.
  - Kein Rückfall von HTTPS auf HTTP.
  - Kein Schalter, der die Zertifikatsprüfung abschaltet.
  - Keine privaten Schlüssel im Repo.
  - Aktualisieren nur über `python tools/vendor_wii_tls.py <checkout> --rev <tag>`.
- **Code:**
  - `src/core/` bindet kein `<ogc/...>` ein, damit es auf dem Host testbar bleibt.
  - Kein `throw` (keine Exceptions), kein RTTI.
  - Der Wii-Build muss mit **0 Warnungen** durchlaufen, mit TLS (`python tools/build_wii.py`) und ohne TLS (`--no-tls`).
- **Fremdmaterial:**
  - Fremde Quellen (Spikes, Referenz-Code) bleiben außerhalb des Repos.
  - Heruntergeladene Dateien bekommen einen eigenen Ordner. Python-Skripte darauf laufen mit `python -I`.

## Commits und PRs

- Commit-Nachrichten im Stil `feat: …`, `fix: …`, `docs: …`, `chore: …`, deutsch.
- Der Nutzer arbeitet hauptsächlich mit Claude Code, das seine eigene Zuschreibung (`Co-Authored-By`) anhängt. Andere Harnesses nutzen ihre eigene.

## Wichtige Befehle

```bash
cmake --preset host && cmake --build --preset host && ctest --preset host   # Host-Tests (core/)
python -m unittest discover -s tools/tests                                  # Tool-Tests
python tools/build_wii.py            # Wii-Build mit TLS  → build/wii/newlauncher.dol
python tools/build_wii.py --no-tls   # Wii-Build ohne TLS → build/wii-notls/
python tools/check_licenses.py build/wii
python tools/dolphin_smoke.py --marker NL_… [--debug key=value …]           # Smoke in Dolphin
python tools/wii_deploy.py E: [--perf on|off]                               # SD-Karte bespielen (vorher fragen!)
python tools/wii_deploy.py --wiiload <IP>                                   # DOL per wiiload in den RAM der Wii
```
