#include "core/cover_download.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <unordered_set>
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
    CHECK(jobs[0].root == "sd:");  // Cover-Wurzel des Geräts, auf dem das Spiel liegt (Spec v2 5.2)
    const Game onUsb2[] = {{Console::Wii, "RSBP01", "SSBB", 0, 0}};
    const Game* usbPtrs[] = {&onUsb2[0]};
    const auto ro = coverDownloadJobs(usbPtrs, [](const std::string&) { return false; }, "sd:/newlauncher/cache/usb2");
    REQUIRE(ro.size() == 1);
    CHECK(ro[0].root == "sd:/newlauncher/cache/usb2");
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
    Result<void> removeFile(std::string_view f) override { return inner.removeFile(f); }
    Result<void> renameFile(std::string_view a, std::string_view b) override { return inner.renameFile(a, b); }
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

TEST_CASE("shouldAutoFetchCovers (M21)") {
    CHECK(shouldAutoFetchCovers({true, true, false, false, false}));
    CHECK_FALSE(shouldAutoFetchCovers({false, true, false, false, false}));  // Einstellung aus
    CHECK_FALSE(shouldAutoFetchCovers({true, false, false, false, false}));  // Netz nicht bereit
    CHECK_FALSE(shouldAutoFetchCovers({true, true, true, false, false}));    // Scan läuft
    CHECK_FALSE(shouldAutoFetchCovers({true, true, false, true, false}));    // anderer Netz-Auftrag
    CHECK_FALSE(shouldAutoFetchCovers({true, true, false, false, true}));    // schon gelaufen
    CHECK_FALSE(shouldAutoFetchCovers({true, true, false, false, false, true}));  // M24: Cover-Cache wird gelöscht
}

// ---- M22: Retro-Cover (libretro-thumbnails) und Fehlliste ----

TEST_CASE("libretroCoverUrl (M22)") {
    const std::string_view base = "https://raw.githubusercontent.com/libretro-thumbnails";
    CHECK(libretroCoverUrl(base, Console::N64, "Glover (USA)") ==
          "https://raw.githubusercontent.com/libretro-thumbnails/Nintendo_-_Nintendo_64/master/Named_Boxarts/"
          "Glover%20%28USA%29.png");
    CHECK(libretroCoverUrl(base, Console::Gba, "Spider-Man - Mysterio's Menace (USA, Europe)") ==
          "https://raw.githubusercontent.com/libretro-thumbnails/Nintendo_-_Game_Boy_Advance/master/Named_Boxarts/"
          "Spider-Man%20-%20Mysterio%27s%20Menace%20%28USA%2C%20Europe%29.png");
    // libretro ersetzt &*/:`<>?\|" durch _; Umlaute als UTF-8-Bytes kodiert
    CHECK(libretroCoverUrl(std::string(base) + "/", Console::Snes, "Tom & Jerry: Ä") ==
          "https://raw.githubusercontent.com/libretro-thumbnails/Nintendo_-_Super_Nintendo_Entertainment_System/"
          "master/Named_Boxarts/Tom%20_%20Jerry_%20%C3%84.png");
    CHECK(libretroCoverUrl(base, Console::Nes, "A").find("/Nintendo_-_Nintendo_Entertainment_System/") !=
          std::string::npos);
    CHECK(libretroCoverUrl(base, Console::Gb, "A").find("/Nintendo_-_Game_Boy/") != std::string::npos);
    CHECK(libretroCoverUrl(base, Console::Gbc, "A").find("/Nintendo_-_Game_Boy_Color/") != std::string::npos);
    CHECK(libretroCoverUrl(base, Console::Wii, "RMCP01").empty());
    CHECK(libretroCoverUrl(base, Console::Channel, "X").empty());
    CHECK(libretroCoverUrl("", Console::Nes, "A").empty());
}

TEST_CASE("coverDownloadJobs: Retro nur mit Basis, Fehlliste wird übersprungen (M22)") {
    const Game games[] = {
        {Console::Nes, "nes/Crystalis (USA).nes", "Crystalis", 0, 0},
        {Console::N64, "n64/Glover (USA).z64", "Glover", 0, 0},
        {Console::Gba, "gba/Da (USA).gba", "Da", 0, 0},
        {Console::Wii, "RMGP01", "SMG", 0, 0},
        {Console::Channel, "HAAA", "Fotokanal", 0, 0},
    };
    const Game* ptrs[] = {&games[0], &games[1], &games[2], &games[3], &games[4]};
    const auto none = [](const std::string&) { return false; };
    CHECK(coverDownloadJobs(ptrs, none, "sd:").size() == 1);  // ohne Retro-Basis wie bisher
    const std::set<std::string> present = {"usb:/newlauncher/cache/covers/gba/Da (USA).tex"};
    const std::unordered_set<std::string> skip = {"wii/RMGP01", "n64/Glover (USA)"};
    const auto jobs = coverDownloadJobs(ptrs, [&](const std::string& p) { return present.count(p) > 0; }, "usb:",
                                        "https://r/t", &skip);
    REQUIRE(jobs.size() == 1);
    CHECK(jobs[0].stem == "nes/Crystalis (USA)");
    CHECK(jobs[0].id6 == "nes/Crystalis (USA)");
    CHECK(jobs[0].root == "usb:");
    REQUIRE_FALSE(jobs[0].urls.empty());
    CHECK(jobs[0].urls[0] == "https://r/t/Nintendo_-_Nintendo_Entertainment_System/master/Named_Boxarts/"
                             "Crystalis%20%28USA%29.png");
}

TEST_CASE("existingCoverFiles mit Retro listet alle Konsolenordner (M22)") {
    CountingFs fs;
    fs.inner.addFile("sd:/newlauncher/covers_src/nes/A.png", {1});
    fs.inner.addFile("sd:/newlauncher/cache/covers/gba/B.tex", {1});
    const auto files = existingCoverFiles(fs, "sd:", true);
    CHECK(fs.lists == 16);  // 8 Konsolen (ohne Kanäle) × 2 Ordner
    CHECK(files.count("sd:/newlauncher/covers_src/nes/A.png") == 1);
    CHECK(files.count("sd:/newlauncher/cache/covers/gba/B.tex") == 1);
}

TEST_CASE("runCoverDownloads: Job mit URL fragt genau diese an, missed meldet Fehlschläge (M22)") {
    FakeNet net;
    std::vector<std::string> missed;
    net.outcomes["http://r/t/x/A.png"] = HttpOutcome::Ok;
    CoverDownloadIo io = net.io();
    io.missed = [&](const std::string& stem) { missed.push_back(stem); };
    const CoverJob jobs[] = {{"nes/A", "nes/A", "sd:", {"https://r/t/x/A.png"}},
                             {"nes/B", "nes/B", "sd:", {"https://r/t/x/B.png"}},
                             {"RMGP01", "wii/RMGP01"}};
    const DownloadSummary s = runCoverDownloads(jobs, kRegions, "http://h/c", io);
    CHECK(s.ok == 1);
    CHECK(s.missing == 2);
    CHECK(net.calls == std::vector<std::string>{"http://r/t/x/A.png", "http://r/t/x/B.png", "http://h/c/EN/RMGP01.png",
                                                "http://h/c/US/RMGP01.png"});
    CHECK(net.saved == std::vector<std::string>{"nes/A"});
    CHECK(missed == std::vector<std::string>{"nes/B", "wii/RMGP01"});
    CHECK(net.log[0] == "NL_COVER_DL nes/A libretro ok");
}

TEST_CASE("Fehlliste: lesen, fortführen, schreiben (M22)") {
    CHECK(parseMissingList("#nl-missing 2\r\nnes/A\r\n\nwii/RMGP01\n  \nnes/A\n") ==
          std::vector<std::string>{"nes/A", "wii/RMGP01"});
    CHECK(parseMissingList("").empty());
    // M24: Listen ohne aktuelle Kopfzeile stammen vom alten Sucher und werden verworfen
    CHECK(parseMissingList("nes/A\nwii/RMGP01\n").empty());
    CHECK(parseMissingList("#nl-missing 1\nnes/A\n").empty());
    // Lauf von Hand: alle Stämme waren Jobs → nur die neuen Fehlschläge bleiben
    CHECK(updateMissingList({"nes/A", "nes/B"}, {"nes/A", "nes/B", "nes/C"}, {"nes/C"}) ==
          std::vector<std::string>{"nes/C"});
    // Automatischer Lauf: übersprungene (keine Jobs) bleiben, neue kommen dazu, sortiert ohne Doppelte
    CHECK(updateMissingList({"wii/X", "nes/B"}, {"nes/C", "nes/D"}, {"nes/D", "nes/D"}) ==
          std::vector<std::string>{"nes/B", "nes/D", "wii/X"});
    CHECK(missingListText({"nes/B", "wii/X"}) == "#nl-missing 2\nnes/B\nwii/X\n");
    CHECK(parseMissingList(missingListText({"nes/B", "wii/X"})) == std::vector<std::string>{"nes/B", "wii/X"});
    CHECK(missingListText({}).empty());
}

TEST_CASE("clearCoverCache löscht PNGs, Texturen und Fehlliste aller Konsolen, sonst nichts (M23)") {
    test::MemFileSystem fs;
    fs.addFile("sd:/newlauncher/covers_src/nes/A.png", {1});
    fs.addFile("sd:/newlauncher/covers_src/wii/RMCP01.png", {1});
    fs.addFile("sd:/newlauncher/cache/covers/gba/B.tex", {1});
    fs.addFile("sd:/newlauncher/cache/covers/missing.txt", {'x', '\n'});
    fs.addFile("sd:/newlauncher/cache/index.bin", {1});      // anderer Cache bleibt
    fs.addFile("sd:/newlauncher/covers_src/readme.txt", {1});  // nicht in einem Konsolenordner
    fs.addFile("sd:/roms/nes/A.nes", {1});
    const CoverClearResult r = clearCoverCache(fs, "sd:");
    CHECK(r.removed == 4);
    CHECK(r.failed == 0);
    CHECK_FALSE(fs.readAll("sd:/newlauncher/covers_src/nes/A.png", 16).isOk());
    CHECK_FALSE(fs.readAll("sd:/newlauncher/covers_src/wii/RMCP01.png", 16).isOk());
    CHECK_FALSE(fs.readAll("sd:/newlauncher/cache/covers/gba/B.tex", 16).isOk());
    CHECK_FALSE(fs.readAll(missingListPath("sd:"), 16).isOk());
    CHECK(fs.readAll("sd:/newlauncher/cache/index.bin", 16).isOk());
    CHECK(fs.readAll("sd:/newlauncher/covers_src/readme.txt", 16).isOk());
    CHECK(fs.readAll("sd:/roms/nes/A.nes", 16).isOk());
}

TEST_CASE("clearCoverCache auf leerer Wurzel ist kein Fehler (M23)") {
    test::MemFileSystem fs;
    const CoverClearResult r = clearCoverCache(fs, "usb:");
    CHECK(r.removed == 0);
    CHECK(r.failed == 0);
}

TEST_CASE("clearCoverCache meldet jede gelöschte Datei an onRemoved (M24)") {
    test::MemFileSystem fs;
    fs.addFile("sd:/newlauncher/covers_src/nes/A.png", {1});
    fs.addFile("sd:/newlauncher/cache/covers/gba/B.tex", {1});
    fs.addFile("sd:/newlauncher/cache/covers/missing.txt", {'x'});
    int calls = 0;
    const CoverClearResult r = clearCoverCache(fs, "sd:", [&] { ++calls; });
    CHECK(r.removed == 3);
    CHECK(calls == 3);
}

TEST_CASE("orderCoverJobs: Generation absteigend, innerhalb einer Konsole stabil (M24)") {
    std::vector<CoverJob> jobs;
    auto add = [&](Console c, const char* stem) {
        CoverJob j{"", stem};
        j.console = c;
        jobs.push_back(j);
    };
    add(Console::Nes, "nes1");
    add(Console::Wii, "wii1");
    add(Console::Snes, "snes1");
    add(Console::Gc, "gc1");
    add(Console::Wii, "wii2");
    add(Console::Gb, "gb1");
    add(Console::Gba, "gba1");
    add(Console::N64, "n641");
    add(Console::Gbc, "gbc1");
    orderCoverJobs(jobs);
    std::string order;
    for (const CoverJob& j : jobs) order += j.stem + ",";
    CHECK(order == "wii1,wii2,gc1,gba1,n641,gbc1,snes1,gb1,nes1,");
    CHECK(coverJobRank(Console::Wii) < coverJobRank(Console::Gc));
    CHECK(coverJobRank(Console::Gb) < coverJobRank(Console::Nes));
}

TEST_CASE("coverDownloadJobs setzt die Konsole des Auftrags (M24)") {
    Game g{};
    g.console = Console::Snes;
    g.key = "snes:F-Zero";
    g.title = "F-Zero";
    g.path = "usb:/roms/snes/F-Zero (Europe).sfc";
    const Game* gs[] = {&g};
    const auto jobs = coverDownloadJobs(gs, [](const std::string&) { return false; }, "usb:", "https://x");
    REQUIRE(jobs.size() == 1);
    CHECK(jobs[0].console == Console::Snes);
}

namespace {
bool hasCandidate(std::string_view stem, std::string_view want) {
    const auto c = libretroNameCandidates(stem);
    return std::find(c.begin(), c.end(), want) != c.end();
}
}  // namespace

TEST_CASE("libretroNameCandidates: exakter Name zuerst, Varianten für abweichende libretro-Namen (M24)") {
    const auto c = libretroNameCandidates("Banjo-Kazooie (USA) (Rev 1)");
    REQUIRE_FALSE(c.empty());
    CHECK(c[0] == "Banjo-Kazooie (USA) (Rev 1)");
    CHECK(hasCandidate("Banjo-Kazooie (USA) (Rev 1)", "Banjo-Kazooie (USA)"));
    CHECK(hasCandidate("Donkey Kong (Japan, USA) (SGB Enhanced)", "Donkey Kong (Japan, USA) (En) (SGB Enhanced)"));
    CHECK(hasCandidate("Gyromite ~ Gyro (World)", "Gyromite (World)"));
    CHECK(hasCandidate("Dr. Mario (Japan, USA) (Rev 1)", "Dr. Mario (Japan, USA) (En) (Rev 1)"));
    CHECK(hasCandidate("Mike Tyson's Punch-Out!! (Japan, USA) (Rev 1)",
                       "Mike Tyson's Punch-Out!! (Japan, USA) (En) (Rev 1)"));
    CHECK(hasCandidate("Super Mario Bros. Deluxe (USA, Europe) (Rev 2)", "Super Mario Bros. Deluxe (USA, Europe)"));
    CHECK(hasCandidate("Shadowgate Classic (USA, Europe) (En,Fr,De,Es,Sv) (Rev 1) (GB Compatible)",
                       "Shadowgate Classic (USA, Europe) (En,Fr,De,Es,Sv) (GB Compatible)"));
    // Mehrfachregion → Einzelregion, Weltausgabe → USA
    CHECK(hasCandidate("Tetris (Japan, USA)", "Tetris (USA)"));
    CHECK(hasCandidate("Galaga (World)", "Galaga (USA)"));
}

TEST_CASE("libretroNameCandidates: begrenzt, ohne Doppelte, kein Japan-Cover für Westausgaben (M24)") {
    const auto c = libretroNameCandidates("A ~ B (USA, Europe) (En,Fr,De) (Rev 1) (SGB Enhanced) (GB Compatible)");
    CHECK(c.size() <= kMaxLibretroCandidates);
    std::set<std::string> uniq(c.begin(), c.end());
    CHECK(uniq.size() == c.size());
    CHECK_FALSE(hasCandidate("Galaga (World)", "Galaga (Japan)"));
    CHECK_FALSE(hasCandidate("Zelda (USA) (Rev 1)", "Zelda (Japan)"));
    CHECK(hasCandidate("Mario Golf 64 (Japan) (Rev 1)", "Mario Golf 64 (Japan)"));
    const auto plain = libretroNameCandidates("Glover");  // ohne Tags
    REQUIRE_FALSE(plain.empty());
    CHECK(plain[0] == "Glover");
    CHECK(hasCandidate("Glover", "Glover (USA)"));
}

TEST_CASE("coverDownloadJobs: Retro-Job trägt alle Kandidaten-URLs (M24)") {
    const Game games[] = {{Console::N64, "n64/Banjo-Kazooie (USA) (Rev 1).z64", "Banjo", 0, 0}};
    const Game* ptrs[] = {&games[0]};
    const auto jobs = coverDownloadJobs(ptrs, [](const std::string&) { return false; }, "sd:", "https://r/t");
    REQUIRE(jobs.size() == 1);
    REQUIRE(jobs[0].urls.size() >= 2);
    CHECK(jobs[0].urls[0] == libretroCoverUrl("https://r/t", Console::N64, "Banjo-Kazooie (USA) (Rev 1)"));
    CHECK(std::find(jobs[0].urls.begin(), jobs[0].urls.end(),
                    libretroCoverUrl("https://r/t", Console::N64, "Banjo-Kazooie (USA)")) != jobs[0].urls.end());
}

TEST_CASE("libretroSymlinkTarget erkennt Git-Symlink-Texte (M24)") {
    const auto bytes = [](std::string_view s) { return std::vector<u8>(s.begin(), s.end()); };
    CHECK(libretroSymlinkTarget(bytes("Banjo-Kazooie (USA).png")) == "Banjo-Kazooie (USA).png");
    CHECK(libretroSymlinkTarget(bytes("Banjo-Kazooie (USA).png\n")) == "Banjo-Kazooie (USA).png");
    CHECK(libretroSymlinkTarget(bytes("404: Not Found")).empty());
    CHECK(libretroSymlinkTarget(bytes("../Named_Snaps/A.png")).empty());  // nur derselbe Ordner
    CHECK(libretroSymlinkTarget(bytes("A.png\nB.png")).empty());
    CHECK(libretroSymlinkTarget(bytes(std::string(600, 'a') + ".png")).empty());
    CHECK(libretroSymlinkTarget(std::vector<u8>{0x89, 'P', 'N', 'G'}).empty());
    CHECK(libretroSymlinkTarget({}).empty());
}

TEST_CASE("libretroLinkUrl ersetzt den Dateinamen der URL (M24)") {
    CHECK(libretroLinkUrl("https://r/t/S/master/Named_Boxarts/A%20%28USA%29%20%28Rev%201%29.png", "A (USA).png") ==
          "https://r/t/S/master/Named_Boxarts/A%20%28USA%29.png");
}

namespace {
// Liefert für bestimmte URLs Text statt PNG (Symlink).
struct LinkNet : FakeNet {
    std::map<std::string, std::string> texts;
    CoverDownloadIo linkIo() {
        CoverDownloadIo io = this->io();
        auto inner = io.get;
        io.get = [this, inner](const Url& url, std::vector<u8>& body) {
            const std::string u = "http://" + url.host + url.path;
            auto it = texts.find(u);
            if (it == texts.end()) return inner(url, body);
            calls.push_back(u);
            body.assign(it->second.begin(), it->second.end());
            return HttpOutcome::Ok;
        };
        return io;
    }
};
}  // namespace

TEST_CASE("runCoverDownloads: folgt Symlinks und probiert weitere Kandidaten (M24)") {
    LinkNet net;
    net.texts["http://r/S/A1.png"] = "A.png";
    net.outcomes["http://r/S/A.png"] = HttpOutcome::Ok;
    net.outcomes["http://r/S/B2.png"] = HttpOutcome::Ok;
    net.texts["http://r/S/C1.png"] = "C1.png";  // Schleife
    std::vector<std::string> missed;
    CoverDownloadIo io = net.linkIo();
    io.missed = [&](const std::string& s) { missed.push_back(s); };
    const CoverJob jobs[] = {{"n64/A", "n64/A", "sd:", {"https://r/S/A1.png", "https://r/S/A0.png"}},
                             {"n64/B", "n64/B", "sd:", {"https://r/S/B1.png", "https://r/S/B2.png"}},
                             {"n64/C", "n64/C", "sd:", {"https://r/S/C1.png"}}};
    const DownloadSummary s = runCoverDownloads(jobs, kRegions, "http://h/c", io);
    CHECK(s.ok == 2);
    CHECK(s.missing == 1);
    CHECK(s.failed == 0);
    CHECK(net.saved == std::vector<std::string>{"n64/A", "n64/B"});
    CHECK(missed == std::vector<std::string>{"n64/C"});
    CHECK(net.calls == std::vector<std::string>{"http://r/S/A1.png", "http://r/S/A.png", "http://r/S/B1.png",
                                                "http://r/S/B2.png", "http://r/S/C1.png", "http://r/S/C1.png",
                                                "http://r/S/C1.png"});
}

TEST_CASE("clearCoverCache bricht zwischen zwei Dateien ab, wenn cancelled meldet (M24)") {
    test::MemFileSystem fs;
    fs.addFile("sd:/newlauncher/covers_src/nes/A.png", {1});
    fs.addFile("sd:/newlauncher/covers_src/nes/B.png", {1});
    fs.addFile("sd:/newlauncher/covers_src/nes/C.png", {1});
    int calls = 0;
    const CoverClearResult r = clearCoverCache(fs, "sd:", [&] { ++calls; }, [&] { return calls >= 1; });
    CHECK(r.removed == 1);
    CHECK(r.failed == 0);
    CHECK(r.cancelled);
    const CoverClearResult rest = clearCoverCache(fs, "sd:");
    CHECK(rest.removed == 2);
    CHECK_FALSE(rest.cancelled);
}
