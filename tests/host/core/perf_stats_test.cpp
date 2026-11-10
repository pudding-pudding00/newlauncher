#include "core/perf_stats.hpp"
#include <doctest/doctest.h>
#include <string>

namespace {
std::string window(nl::PerfStats& p) {
    char buf[160];
    p.takeWindow(buf, sizeof buf);
    return buf;
}
}  // namespace

TEST_CASE("60 Hz: 25,0 ms ist nicht langsam, 25,1 ms schon") {
    nl::PerfStats p(60);
    p.frame(25.0f, false);
    CHECK(p.slowFrames() == 0);
    p.frame(25.1f, false);
    CHECK(p.slowFrames() == 1);
}

TEST_CASE("50 Hz: 30,0 ms ist nicht langsam, 34 ms schon") {
    nl::PerfStats p(50);
    p.frame(20.0f, false);
    p.frame(30.0f, false);
    CHECK(p.slowFrames() == 0);
    p.frame(34.0f, false);
    CHECK(p.slowFrames() == 1);
}

TEST_CASE("Fenster nach 300 Frames voll; takeWindow formatiert und setzt zurück") {
    nl::PerfStats p(60);
    for (int i = 0; i < 300; ++i) {
        CHECK_FALSE(p.windowFull());
        p.frame(i == 150 ? 40.0f : 16.7f, i == 9);
    }
    CHECK(p.windowFull());
    CHECK(window(p) == "NL_PERF frames n=300 slow=1 max_ms=40.0 busy=1");
    CHECK_FALSE(p.windowFull());
    p.frame(16.7f, false);
    for (int i = 0; i < 299; ++i) p.frame(16.7f, false);
    CHECK(window(p) == "NL_PERF frames n=300 slow=0 max_ms=16.7 busy=0");
}

TEST_CASE("Perzentile nach Nearest-Rank") {
    nl::PerfStats p(60);
    for (int i = 100; i >= 1; --i) p.coverLoaded(static_cast<float>(i));
    char buf[400];
    p.formatSummary(buf, sizeof buf, 0, 0, 0);
    const std::string s = buf;
    CHECK(s.find("cover_n=100 cover_p50=50.0 cover_p95=95.0 cover_max=100.0") != std::string::npos);
}

TEST_CASE("Ring behält die letzten 1024 Latenzen") {
    nl::PerfStats p(60);
    for (int i = 0; i < 1100; ++i) p.coverLoaded(i < 76 ? 1000.0f : 10.0f);
    char buf[400];
    p.formatSummary(buf, sizeof buf, 0, 0, 0);
    const std::string s = buf;
    CHECK(s.find("cover_n=1024 cover_p50=10.0 cover_p95=10.0 cover_max=10.0") != std::string::npos);
}

TEST_CASE("ohne Cover: Nullwerte") {
    nl::PerfStats p(60);
    char buf[400];
    p.formatSummary(buf, sizeof buf, 0, 0, 0);
    CHECK(std::string(buf).find("cover_n=0 cover_p50=0.0 cover_p95=0.0 cover_max=0.0") != std::string::npos);
}

TEST_CASE("formatSummary enthält alle Felder in der festen Reihenfolge") {
    // Dieselbe Zeile prüft tools/tests/test_check_perf.py (test_pass_all).
    nl::PerfStats p(60);
    p.setBoot(180, 1420, 900);
    for (int i = 0; i < 299; ++i) p.frame(16.7f, false);
    p.frame(40.0f, false);
    (void)window(p);  // Gesamtzähler laufen über Fenster hinweg
    p.frame(16.7f, false);
    for (int i = 1; i <= 100; ++i) p.coverLoaded(static_cast<float>(i) / 2.0f);
    char buf[400];
    p.formatSummary(buf, sizeof buf, 1000, 2000, 3000);
    CHECK(std::string(buf) ==
          "NL_PERF summary ready_ms=1420 usb_wait_ms=900 hz=60 frames=301 slow=1 busy_slow=0 max_ms=40.0 cover_n=100 "
          "cover_p50=25.0 cover_p95=47.5 cover_max=50.0 mem1_free=1000 mem2_free=2000 heap_free=3000");
}

TEST_CASE("pause: der nächste Abstand zählt nicht (eigener Log-Flush)") {
    nl::PerfStats p(60);
    p.frame(16.7f, false);
    p.pause();
    p.frame(120.0f, true);  // enthält den Flush: übersprungen
    p.frame(40.0f, false);  // danach wird wieder gemessen
    CHECK(p.slowFrames() == 1);
    CHECK(window(p) == "NL_PERF frames n=2 slow=1 max_ms=40.0 busy=0");
}

TEST_CASE("busy_slow: langsame Frames während Hintergrundarbeit, über Fenster hinweg") {
    nl::PerfStats p(60);
    p.frame(40.0f, true);
    p.frame(40.0f, false);
    p.frame(16.0f, true);
    (void)window(p);
    p.frame(30.0f, true);
    CHECK(p.busySlowFrames() == 2);
    char buf[400];
    p.formatSummary(buf, sizeof buf, 0, 0, 0);
    CHECK(std::string(buf).find("frames=4 slow=3 busy_slow=2 ") != std::string::npos);
}

TEST_CASE("formatBoot") {
    nl::PerfStats p(60);
    p.setBoot(180, 1420, 900);
    char buf[160];
    p.formatBoot(buf, sizeof buf);
    CHECK(std::string(buf) == "NL_PERF boot first_frame_ms=180 ready_ms=1420 usb_wait_ms=900 hz=60");
}

TEST_CASE("kleiner Puffer: abgeschnitten, nullterminiert") {
    nl::PerfStats p(60);
    char buf[8];
    p.formatBoot(buf, sizeof buf);
    CHECK(std::string(buf) == "NL_PERF");
}
