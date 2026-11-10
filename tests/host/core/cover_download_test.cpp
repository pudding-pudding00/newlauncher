#include "core/cover_download.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <map>
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

#include "support/mem_fs.hpp"

namespace {
// Zählt list()-Aufrufe und reicht alles an ein MemFileSystem durch.
struct CountingFs final : FileSystem {
    test::MemFileSystem inner;
    int lists = 0;
    Result<std::vector<DirEntry>> list(std::string_view dir) override {
        ++lists;
        return inner.list(dir);
    }
    Result<std::size_t> read(std::string_view f, u64 o, std::span<u8> out) override { return inner.read(f, o, out); }
    Result<std::vector<u8>> readAll(std::string_view f, std::size_t m) override { return inner.readAll(f, m); }
    Result<void> writeAtomic(std::string_view f, std::span<const u8> d) override { return inner.writeAtomic(f, d); }
    Result<void> makeDirs(std::string_view d) override { return inner.makeDirs(d); }
};
}  // namespace

TEST_CASE("existingCoverFiles listet jeden Cover-Ordner einmal") {
    CountingFs fs;
    fs.inner.addFile("sd:/newlauncher/covers_src/wii/RMCP01.png", {1});
    fs.inner.addFile("sd:/newlauncher/cache/covers/gc/GM8P01.tex", {1});
    for (int i = 0; i < 50; ++i) fs.inner.addFile("sd:/newlauncher/covers_src/nes/Spiel " + std::to_string(i) + ".png", {1});
    const auto files = existingCoverFiles(fs, "sd:");
    CHECK(fs.lists == 4);  // covers_src/wii, covers_src/gc, cache/covers/wii, cache/covers/gc (fehlende Ordner zählen mit)
    CHECK(files.count("sd:/newlauncher/covers_src/wii/RMCP01.png") == 1);
    CHECK(files.count("sd:/newlauncher/cache/covers/gc/GM8P01.tex") == 1);
    CHECK(files.size() == 2);  // Retro-Cover braucht der Download nicht
}

namespace {
struct FakeNet {
    std::map<std::string, HttpOutcome> outcomes;  // URL → Ergebnis, sonst NotFound
    std::vector<std::string> calls, saved, finishedStems, log;
    int cancelAfterCalls = -1;
    u32 jobsDone = 0;
    CoverDownloadIo io() {
        CoverDownloadIo io;
        io.get = [this](const Url& url, std::vector<u8>& body) {
            const std::string u = "http://" + url.host + url.path;
            calls.push_back(u);
            body.clear();
            auto it = outcomes.find(u);
            const HttpOutcome o = it == outcomes.end() ? HttpOutcome::NotFound : it->second;
            if (o == HttpOutcome::Ok) body = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 1};
            return o;
        };
        io.save = [this](const std::string& stem, std::span<const u8>) {
            saved.push_back(stem);
            return true;
        };
        io.cancelled = [this] { return cancelAfterCalls >= 0 && static_cast<int>(calls.size()) >= cancelAfterCalls; };
        io.log = [this](std::string line) { log.push_back(std::move(line)); };
        io.finished = [this](const std::string& stem) { finishedStems.push_back(stem); };
        io.jobDone = [this] { ++jobsDone; };
        return io;
    }
};
const std::string kRegions[] = {"EN", "US"};
}  // namespace

TEST_CASE("runCoverDownloads: zweite Region trifft, Marker und Speichern") {
    FakeNet net;
    net.outcomes["http://h/c/US/RMCP01.png"] = HttpOutcome::Ok;
    const CoverJob jobs[] = {{"RMCP01", "wii/RMCP01"}, {"RMGP01", "wii/RMGP01"}};
    const DownloadSummary s = runCoverDownloads(jobs, kRegions, "http://h/c", net.io());
    CHECK(s.ok == 1);
    CHECK(s.missing == 1);
    CHECK(s.failed == 0);
    CHECK(net.saved == std::vector<std::string>{"wii/RMCP01"});
    CHECK(net.finishedStems == std::vector<std::string>{"wii/RMCP01"});
    CHECK(net.log == std::vector<std::string>{"NL_COVER_DL RMCP01 US ok", "NL_COVER_DL RMGP01 missing"});
    CHECK(net.jobsDone == 2);
}

TEST_CASE("runCoverDownloads: Abbruch wird vor jeder Region geprüft") {
    FakeNet net;
    net.cancelAfterCalls = 1;
    const CoverJob jobs[] = {{"RMCP01", "wii/RMCP01"}, {"RMGP01", "wii/RMGP01"}};
    const DownloadSummary s = runCoverDownloads(jobs, kRegions, "http://h/c", net.io());
    CHECK(net.calls.size() == 1);
    CHECK(s.failed == 2);
    CHECK(net.log == std::vector<std::string>{"NL_COVER_DL RMCP01 failed", "NL_COVER_DL RMGP01 failed"});
    CHECK(net.jobsDone == 2);
}

TEST_CASE("runCoverDownloads: nach drei Fehlern in Folge kein weiterer Versuch") {
    FakeNet net;
    const char* ids[] = {"A", "B", "C", "D", "E", "F", "G"};
    std::vector<CoverJob> jobs;
    for (const char* id : ids) {
        jobs.push_back({id, std::string("wii/") + id});
        net.outcomes[std::string("http://h/c/EN/") + id + ".png"] = HttpOutcome::Failed;
    }
    net.outcomes.erase("http://h/c/EN/C.png");  // C: nicht gefunden → setzt die Serie zurück
    const DownloadSummary s = runCoverDownloads(jobs, kRegions, "http://h/c", net.io());
    // A, B scheitern; C fragt EN und US; D, E, F scheitern; G wird nicht mehr versucht.
    CHECK(net.calls.size() == 7);
    CHECK(s.failed == 6);
    CHECK(s.missing == 1);
    CHECK(net.log.back() == "NL_COVER_DL G failed");
    CHECK(std::find(net.log.begin(), net.log.end(), "NL_COVER_DL_ABORT failures=3") != net.log.end());
    CHECK(net.jobsDone == 7);
}

TEST_CASE("runCoverDownloads: 200 ohne PNG gilt als nicht gefunden") {
    FakeNet net;
    CoverDownloadIo io = net.io();
    io.get = [&](const Url&, std::vector<u8>& body) {
        net.calls.emplace_back("x");
        body = {'<', 'h', 't', 'm', 'l', '>', 0, 0, 0};
        return HttpOutcome::Ok;
    };
    const CoverJob jobs[] = {{"RMCP01", "wii/RMCP01"}};
    const DownloadSummary s = runCoverDownloads(jobs, kRegions, "http://h/c", io);
    CHECK(s.missing == 1);
    CHECK(net.saved.empty());
}
