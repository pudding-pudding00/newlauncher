#include "core/cover_download.hpp"
#include <doctest/doctest.h>
#include <set>
#include <string>
#include <vector>
using namespace nl;

namespace {
std::string joined(const std::vector<std::string_view>& v) {
    std::string s;
    for (auto r : v) s += std::string(s.empty() ? "" : ",") + std::string(r);
    return s;
}
}  // namespace

TEST_CASE("coverRegions") {
    CHECK(joined(coverRegions("auto", "DE")) == "DE,EN,US,JA");
    CHECK(joined(coverRegions("auto", "EN")) == "EN,US,JA");
    CHECK(joined(coverRegions("US", "DE")) == "US,EN,JA");
    CHECK(joined(coverRegions("JA", "JA")) == "JA,EN,US");
    CHECK(joined(coverRegions("FR", "DE")) == "FR,EN,US,JA");
    CHECK(joined(coverRegions("xx", "EN")) == "EN,US,JA");
}

TEST_CASE("gametdbCoverUrl") {
    CHECK(gametdbCoverUrl("http://art.gametdb.com/wii/cover", "EN", "RMCP01") ==
          "http://art.gametdb.com/wii/cover/EN/RMCP01.png");
    CHECK(gametdbCoverUrl("http://127.0.0.1:8000/", "US", "GM8P01") == "http://127.0.0.1:8000/US/GM8P01.png");
}

TEST_CASE("coverDownloadJobs nur Wii/GC ohne PNG und ohne .tex") {
    const Game games[] = {
        {Console::Wii, "RMCP01", "MK", 0, 0},
        {Console::Wii, "RMGP01", "SMG", 0, 0},
        {Console::Gc, "GM8P01", "MP", 0, 0},
        {Console::Nes, "nes/A.nes", "A", 0, 0},
    };
    const Game* ptrs[] = {&games[0], &games[1], &games[2], &games[3]};
    const std::set<std::string> present = {"sd:/newlauncher/covers_src/wii/RMCP01.png",
                                           "sd:/newlauncher/cache/covers/gc/GM8P01.tex"};
    const auto jobs = coverDownloadJobs(ptrs, [&](const std::string& p) { return present.count(p) > 0; }, "sd:");
    REQUIRE(jobs.size() == 1);
    CHECK(jobs[0].id6 == "RMGP01");
    CHECK(jobs[0].stem == "wii/RMGP01");
}

TEST_CASE("isPng") {
    const u8 png[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0};
    const u8 html[] = {'<', 'h', 't', 'm', 'l', '>', 0, 0, 0};
    CHECK(isPng(png));
    CHECK_FALSE(isPng(html));
    CHECK_FALSE(isPng(std::span<const u8>(png, 7)));
}
