#include "core/frame_stats.hpp"
#include <doctest/doctest.h>
#include <cstring>
#include <string_view>
using nl::FrameStats;
using nl::formatStats;

TEST_CASE("formatStats formatiert alle Felder") {
    char buf[64];
    formatStats(FrameStats{4200, 1, 310, 0, 524288}, buf, sizeof buf);
    CHECK(std::string_view(buf) == "CPU 4.2 ms | DC 1 | Q 310 | Tex 512 KB");
}

TEST_CASE("formatStats zeigt DROP nur bei verworfenen Quads") {
    char buf[64];
    formatStats(FrameStats{16000, 2, 4096, 12, 1024}, buf, sizeof buf);
    CHECK(std::string_view(buf) == "CPU 16.0 ms | DC 2 | Q 4096 | Tex 1 KB | DROP 12");
}

TEST_CASE("formatStats schneidet bei kleinem Puffer sauber ab") {
    char buf[10];
    formatStats(FrameStats{4200, 1, 310, 0, 524288}, buf, sizeof buf);
    CHECK(std::strlen(buf) == 9);
    char none[1] = {'x'};
    CHECK(formatStats(FrameStats{}, none, 0) == 0);
    CHECK(none[0] == 'x');
}

TEST_CASE("formatStats mit Maximalwerten läuft nicht über") {
    char buf[64];
    const nl::u32 m = 0xFFFFFFFFu;
    const int n = formatStats(FrameStats{m, m, m, m, m}, buf, sizeof buf);
    CHECK(n == static_cast<int>(std::strlen(buf)));
    CHECK(n <= 63);
}
