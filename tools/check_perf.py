"""Bewertet die NL_PERF-Zeilen aus sd:/newlauncher/log.txt gegen die Budgets aus Spec §7 (M7).

    python tools/check_perf.py <log.txt> [<weitere log.txt> ...]

Mehrere Dateien werden zusammengeführt, z. B. das Log der Release-DOL (NL_PERF) und das der
Selbsttest-DOL (NL_ALLOC_IDLE). Ausgabe: Markdown-Tabelle für docs/hardware-acceptance.md.
Exit 0 ohne FAIL, 1 bei mindestens einem FAIL, 2 wenn keine Datei eine NL_PERF summary enthält.

log.txt ist ein Ringpuffer: frühe Zeilen (boot, erste Fenster) können fehlen. Geurteilt wird deshalb
aus der letzten Summary; Boot- und Fensterzeilen ergänzen nur, was die Summary nicht enthält.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

START_BUDGET_MS = 2000
COVER_P95_BUDGET_MS = 100
COVER_MAX_WARN_MS = 250
SPLASH_TARGET_MS = 500
TLS_HEAP_TARGET_BYTES = 160 * 1024  # Spec v2 4.6: Zielwerte je TLS-Verbindung
TLS_HANDSHAKE_TARGET_MS = 3000

_PAIR = re.compile(r"(\w+)=(-?\d+(?:\.\d+)?)")
_MARKER = re.compile(r"NL_PERF (boot|frames|summary) (.*)$")
_ALLOC = re.compile(r"NL_ALLOC_IDLE (\d+)")
_TLS = re.compile(r"NL_PERF tls (.*)$")


@dataclass
class PerfLog:
    boot: dict | None = None
    windows: list[dict] = field(default_factory=list)
    summary: dict | None = None
    alloc_idle: int | None = None
    tls: list[dict] = field(default_factory=list)  # je erfolgreichem Handshake: heap (Bytes), ms


@dataclass
class Check:
    name: str
    value: str
    budget: str
    status: str  # PASS, WARN, FAIL, N/A


def _pairs(rest: str) -> dict:
    out = {}
    for key, raw in _PAIR.findall(rest):
        out[key] = float(raw) if "." in raw else int(raw)
    return out


def parse_log(text: str) -> PerfLog:
    log = PerfLog()
    for line in text.splitlines():
        if m := _MARKER.search(line):
            kind, values = m.group(1), _pairs(m.group(2))
            if kind == "boot":
                log.boot = values
            elif kind == "frames":
                log.windows.append(values)
            else:
                log.summary = values  # die letzte zählt
        elif m := _ALLOC.search(line):
            log.alloc_idle = int(m.group(1))
        elif m := _TLS.search(line):
            log.tls.append(_pairs(m.group(1)))
    return log


def evaluate(log: PerfLog) -> list[Check]:
    s = log.summary or {}
    checks = []

    if "ready_ms" in s:
        start = s["ready_ms"] - s.get("usb_wait_ms", 0)
        checks.append(Check("Start", f"{start} ms (ohne USB {s.get('usb_wait_ms', 0)} ms)", f"≤ {START_BUDGET_MS} ms",
                            "PASS" if start <= START_BUDGET_MS else "FAIL"))
    else:
        checks.append(Check("Start", "keine Summary", f"≤ {START_BUDGET_MS} ms", "N/A"))

    hz = int(s.get("hz", 60))
    if "slow" in s:
        value = f"{s['slow']} langsame von {s.get('frames', 0)} Frames, max {s.get('max_ms', 0)} ms"
        checks.append(Check("Bildrate", value, f"konstant {hz} fps", "PASS" if s["slow"] == 0 else "FAIL"))
    else:
        checks.append(Check("Bildrate", "keine Summary", f"konstant {hz} fps", "N/A"))

    cover_budget = f"p95 ≤ {COVER_P95_BUDGET_MS} ms (max > {COVER_MAX_WARN_MS} ms: WARN)"
    if s.get("cover_n", 0) == 0:
        checks.append(Check("Cover", "keine Cover-Ladevorgänge gemessen", cover_budget, "N/A"))
    else:
        value = f"n={s['cover_n']} p50={s['cover_p50']} p95={s['cover_p95']} max={s['cover_max']} ms"
        if s["cover_p95"] > COVER_P95_BUDGET_MS:
            status = "FAIL"
        elif s["cover_max"] > COVER_MAX_WARN_MS:
            status = "WARN"
        else:
            status = "PASS"
        checks.append(Check("Cover", value, cover_budget, status))

    blocked = [i for i, w in enumerate(log.windows, 1) if w.get("busy") == 1 and w.get("slow", 0) > 0]
    busy = sum(1 for w in log.windows if w.get("busy") == 1)
    seen = sum(w.get("n", 0) for w in log.windows)
    gap = " (frühe Fensterzeilen fehlen im Log, Ringpuffer)" if seen < s.get("frames", 0) else ""
    if "busy_slow" in s:
        # Die Summary zählt über die ganze Sitzung; Fensterzeilen sind nur ein Hinweis, wo.
        where = f" (Fenster {', '.join(map(str, blocked))} im Log)" if blocked else ""
        value = f"{s['busy_slow']} langsame Frames während Hintergrundarbeit{where}{gap}"
        checks.append(Check("Blockierung", value, "keine", "FAIL" if s["busy_slow"] > 0 else "PASS"))
    elif blocked:
        checks.append(Check("Blockierung", "langsame Frames in Fenster " + ", ".join(map(str, blocked)), "keine",
                            "FAIL"))
    elif log.windows:
        checks.append(Check("Blockierung", f"{busy} von {len(log.windows)} Fenstern mit Hintergrundarbeit, "
                                           "alle ohne langsame Frames", "keine", "PASS"))
    elif s.get("slow", None) == 0:
        checks.append(Check("Blockierung", "keine langsamen Frames (Fensterzeilen fehlen im Log)", "keine", "PASS"))
    else:
        checks.append(Check("Blockierung", "Fensterzeilen fehlen im Log (Ringpuffer)", "keine", "N/A"))

    if log.alloc_idle is None:
        checks.append(Check("Allokationen", "kein NL_ALLOC_IDLE: Log der Selbsttest-Karte angeben", "0", "N/A"))
    else:
        checks.append(Check("Allokationen", str(log.alloc_idle), "0", "PASS" if log.alloc_idle == 0 else "FAIL"))

    # Spec §6.1 nennt den Splash als Ziel, nicht als Budget: höchstens WARN.
    splash_budget = f"≤ {SPLASH_TARGET_MS} ms (Ziel)"
    if log.boot and "first_frame_ms" in log.boot:
        ms = log.boot["first_frame_ms"]
        checks.append(Check("Splash", f"{ms} ms", splash_budget, "PASS" if ms <= SPLASH_TARGET_MS else "WARN"))
    else:
        checks.append(Check("Splash", "NL_PERF boot fehlt im Log (Ringpuffer)", splash_budget, "N/A"))

    # Spec v2 4.6 nennt Zielwerte; eine Abweichung wird dokumentiert: höchstens WARN.
    tls_budget = f"Heap ≤ {TLS_HEAP_TARGET_BYTES} B, Handshake ≤ {TLS_HANDSHAKE_TARGET_MS} ms (Ziel)"
    if log.tls:
        # heap ist die uordblks-Differenz um den Handshake; gibt der Haupt-Thread parallel frei, wird sie negativ und
        # ist keine Messung. Aussagekräftig nur bei ruhender Oberfläche (docs/acceptance/m11-wii.md, W4).
        heaps = [t["heap"] for t in log.tls if t.get("heap", -1) >= 0]
        ms = max(t.get("ms", 0) for t in log.tls)
        heap_text = f"max heap={max(heaps)} B" if heaps else "heap nicht messbar"
        ok = (not heaps or max(heaps) <= TLS_HEAP_TARGET_BYTES) and ms <= TLS_HANDSHAKE_TARGET_MS
        checks.append(Check("TLS", f"n={len(log.tls)} {heap_text}, max handshake={ms} ms", tls_budget,
                            "PASS" if ok else "WARN"))
    else:
        checks.append(Check("TLS", "kein NL_PERF tls (keine HTTPS-Verbindung oder perf aus)", tls_budget, "N/A"))
    return checks


def markdown(checks: list[Check], log: PerfLog) -> str:
    lines = ["| Kriterium | Messwert | Budget | Ergebnis |", "|---|---|---|---|"]
    lines += [f"| {c.name} | {c.value} | {c.budget} | {c.status} |" for c in checks]
    s = log.summary or {}
    if s:
        lines.append("")
        lines.append(f"Speicher frei: MEM1 {s.get('mem1_free', 0)} B, MEM2 {s.get('mem2_free', 0)} B, "
                     f"Heap {s.get('heap_free', 0)} B")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("logs", nargs="+", type=Path, help="log.txt von der SD-Karte")
    args = parser.parse_args(argv)
    texts = [p.read_text(encoding="utf-8", errors="replace") for p in args.logs]
    log = parse_log("\n".join(texts))
    if log.summary is None:
        print("check_perf: keine NL_PERF summary gefunden ([debug] perf = 1 gesetzt? Launcher sauber beendet?)")
        return 2
    checks = evaluate(log)
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")  # „≤“ in der Tabelle; die Windows-Konsole ist sonst cp1252
    print(markdown(checks, log))
    return 1 if any(c.status == "FAIL" for c in checks) else 0


if __name__ == "__main__":
    sys.exit(main())
