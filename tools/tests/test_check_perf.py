import io
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from check_perf import evaluate, main, parse_log  # noqa: E402

# Dieselbe Zeile erzeugt tests/host/core/perf_stats_test.cpp („formatSummary enthält alle Felder …“).
SUMMARY = ("NL_PERF summary ready_ms=1420 usb_wait_ms=900 hz=60 frames=301 slow=1 busy_slow=0 max_ms=40.0 cover_n=100 "
           "cover_p50=25.0 cover_p95=47.5 cover_max=50.0 mem1_free=1000 mem2_free=2000 heap_free=3000")


def summary(**kw) -> str:
    fields = {"ready_ms": 1420, "usb_wait_ms": 900, "hz": 60, "frames": 3000, "slow": 0, "max_ms": "17.0",
              "cover_n": 100, "cover_p50": "25.0", "cover_p95": "47.5", "cover_max": "50.0",
              "mem1_free": 1000, "mem2_free": 2000, "heap_free": 3000}
    fields.update(kw)
    return "NL_PERF summary " + " ".join(f"{k}={v}" for k, v in fields.items())


GOOD = "\n".join([
    "newlauncher 0.7.0",
    "NL_READY",
    "NL_PERF boot first_frame_ms=180 ready_ms=1420 usb_wait_ms=900 hz=60",
    "NL_PERF frames n=300 slow=0 max_ms=17.0 busy=1",
    "NL_PERF frames n=300 slow=0 max_ms=16.9 busy=0",
    "NL_ALLOC_IDLE 0",
    "NL_PERF tls heap=98304 ms=1200",
    summary(),
])


def by_name(checks):
    return {c.name: c for c in checks}


class CheckPerfTest(unittest.TestCase):
    def run_cli(self, *texts):
        with tempfile.TemporaryDirectory() as d:
            paths = []
            for i, t in enumerate(texts):
                p = Path(d) / f"log{i}.txt"
                p.write_text(t, encoding="utf-8")
                paths.append(str(p))
            out = io.StringIO()
            with redirect_stdout(out):
                code = main(paths)
            return code, out.getvalue()

    def test_pass_all(self):
        checks = by_name(evaluate(parse_log(GOOD)))
        self.assertEqual({c.status for c in checks.values()}, {"PASS"})
        self.assertEqual(set(checks), {"Start", "Bildrate", "Cover", "Blockierung", "Allokationen", "Splash", "TLS"})
        code, out = self.run_cli(GOOD)
        self.assertEqual(code, 0)
        self.assertIn("| Start |", out)

    def test_shared_summary_line_parses(self):
        log = parse_log(SUMMARY)
        self.assertEqual(log.summary["ready_ms"], 1420)
        self.assertEqual(log.summary["cover_p95"], 47.5)
        self.assertEqual(by_name(evaluate(log))["Bildrate"].status, "FAIL")

    def test_usb_wait_excluded(self):
        checks = by_name(evaluate(parse_log(summary(ready_ms=2900, usb_wait_ms=1000))))
        self.assertEqual(checks["Start"].status, "PASS")
        checks = by_name(evaluate(parse_log(summary(ready_ms=3100, usb_wait_ms=1000))))
        self.assertEqual(checks["Start"].status, "FAIL")

    def test_blocking_window_named(self):
        text = "\n".join([
            "NL_PERF frames n=300 slow=0 max_ms=17.0 busy=0",
            "NL_PERF frames n=300 slow=0 max_ms=17.0 busy=1",
            "NL_PERF frames n=300 slow=2 max_ms=40.0 busy=1",
            summary(slow=2),
        ])
        checks = by_name(evaluate(parse_log(text)))
        self.assertEqual(checks["Blockierung"].status, "FAIL")
        self.assertIn("3", checks["Blockierung"].value)
        self.assertEqual(checks["Bildrate"].status, "FAIL")

    def test_cover_warn_on_outlier(self):
        checks = by_name(evaluate(parse_log(summary(cover_p95="80.0", cover_max="400.0"))))
        self.assertEqual(checks["Cover"].status, "WARN")
        checks = by_name(evaluate(parse_log(summary(cover_p95="120.0"))))
        self.assertEqual(checks["Cover"].status, "FAIL")
        checks = by_name(evaluate(parse_log(summary(cover_n=0))))
        self.assertEqual(checks["Cover"].status, "N/A")

    def test_last_summary_wins(self):
        log = parse_log(summary(ready_ms=5000) + "\n" + summary(ready_ms=1000))
        self.assertEqual(log.summary["ready_ms"], 1000)

    def test_ring_truncated_log(self):
        # Ringpuffer: der Anfang ist abgeschnitten, nur ein Zeilenrest und die Summary sind übrig.
        text = "max_ms=17.0 busy=0\n" + summary()
        checks = by_name(evaluate(parse_log(text)))
        self.assertEqual(checks["Start"].status, "PASS")
        self.assertEqual(checks["Bildrate"].status, "PASS")
        self.assertEqual(checks["Splash"].status, "N/A")
        self.assertEqual(checks["Blockierung"].status, "PASS")  # ohne langsame Frames kann nichts blockiert haben
        self.assertEqual(checks["Allokationen"].status, "N/A")
        self.assertIn("Selbsttest-Karte", checks["Allokationen"].value)

    def test_blocking_from_summary_when_windows_dropped(self):
        # Ringpuffer: das blockierte Startfenster ist weg, die übrigen Fenster sind sauber.
        text = "NL_PERF frames n=300 slow=0 max_ms=17.0 busy=0\n" + summary(slow=2, busy_slow=2)
        checks = by_name(evaluate(parse_log(text)))
        self.assertEqual(checks["Blockierung"].status, "FAIL")
        self.assertIn("2", checks["Blockierung"].value)
        self.assertIn("Ringpuffer", checks["Blockierung"].value)

    def test_blocking_pass_from_summary_without_windows(self):
        checks = by_name(evaluate(parse_log(summary(slow=3, busy_slow=0))))
        self.assertEqual(checks["Blockierung"].status, "PASS")
        self.assertEqual(checks["Bildrate"].status, "FAIL")

    def test_shared_summary_has_busy_slow(self):
        self.assertEqual(parse_log(SUMMARY).summary["busy_slow"], 0)

    def test_blocking_unknown_without_windows(self):
        checks = by_name(evaluate(parse_log(summary(slow=3))))
        self.assertEqual(checks["Blockierung"].status, "N/A")

    def test_splash_warn_never_fail(self):
        text = "NL_PERF boot first_frame_ms=900 ready_ms=1420 usb_wait_ms=0 hz=60\n" + summary()
        self.assertEqual(by_name(evaluate(parse_log(text)))["Splash"].status, "WARN")

    def test_alloc_nonzero_fails(self):
        checks = by_name(evaluate(parse_log("NL_ALLOC_IDLE 4\n" + summary())))
        self.assertEqual(checks["Allokationen"].status, "FAIL")

    def test_no_summary_exit_2(self):
        code, _ = self.run_cli("NL_READY\nNL_ALLOC_IDLE 0\n")
        self.assertEqual(code, 2)

    def test_fail_exit_1(self):
        code, out = self.run_cli(summary(slow=5))
        self.assertEqual(code, 1)
        self.assertIn("FAIL", out)

    def test_files_merged(self):
        # Release-Log mit Summary, Selbsttest-Log mit NL_ALLOC_IDLE
        code, out = self.run_cli(summary(), "NL_ALLOC_IDLE 0\n")
        self.assertEqual(code, 0)
        self.assertNotIn("N/A", out.split("| Allokationen |")[1].splitlines()[0])

    def test_pal50_budget_text(self):
        checks = by_name(evaluate(parse_log(summary(hz=50))))
        self.assertIn("50 fps", checks["Bildrate"].budget)
        checks = by_name(evaluate(parse_log(summary())))
        self.assertIn("60 fps", checks["Bildrate"].budget)


    def test_tls_worst_handshake_reported(self):
        # Spec v2 4.6: je Verbindung <= 160 KiB Heap, Handshake <= 3 s; gewertet wird der schlechteste Wert.
        log = summary() + "\nNL_PERF tls heap=100000 ms=900\nNL_PERF tls heap=150000 ms=2500\n"
        tls = by_name(evaluate(parse_log(log)))["TLS"]
        self.assertEqual(tls.status, "PASS")
        self.assertIn("n=2", tls.value)
        self.assertIn("150000", tls.value)
        self.assertIn("2500", tls.value)
        self.assertIn("163840", tls.budget)
        self.assertIn("3000", tls.budget)

    def test_tls_over_target_warns_not_fails(self):
        # Spec v2 4.6 nennt Zielwerte: Abweichung wird dokumentiert (WARN), nicht als FAIL gewertet.
        for line in ("NL_PERF tls heap=200000 ms=900", "NL_PERF tls heap=1000 ms=4000"):
            tls = by_name(evaluate(parse_log(summary() + "\n" + line)))["TLS"]
            self.assertEqual(tls.status, "WARN", line)
        code, _ = self.run_cli(GOOD + "\nNL_PERF tls heap=200000 ms=900")
        self.assertEqual(code, 0)

    def test_tls_negative_heap_ignored(self):
        # uordblks-Differenz: der Haupt-Thread gibt parallel frei, negative Werte sind keine Messung.
        log = summary() + "\nNL_PERF tls heap=-400000 ms=900\nNL_PERF tls heap=36536 ms=800\n"
        tls = by_name(evaluate(parse_log(log)))["TLS"]
        self.assertEqual(tls.status, "PASS")
        self.assertIn("max heap=36536 B", tls.value)
        only_negative = by_name(evaluate(parse_log(summary() + "\nNL_PERF tls heap=-5 ms=900")))["TLS"]
        self.assertIn("heap nicht messbar", only_negative.value)
        self.assertEqual(only_negative.status, "PASS")

    def test_tls_missing_is_na(self):
        self.assertEqual(by_name(evaluate(parse_log(summary())))["TLS"].status, "N/A")


if __name__ == "__main__":
    unittest.main()
