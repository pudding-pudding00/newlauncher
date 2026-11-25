#include "core/library_merge.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "core/cover_paths.hpp"
#include "support/mem_fs.hpp"

using namespace nl;

namespace {
IndexGame nes(const char* file, const char* title) {
    return IndexGame{Console::Nes, 0, std::string("nes/") + file, title, std::string("roms/nes/") + file, 16};
}
DeviceIndex dev(const char* name, std::vector<IndexGame> games) {
    DeviceIndex d{name, {}};
    d.index.games = std::move(games);
    return d;
}
const MergedGame* find(const MergedLibrary& lib, std::string_view key) {
    for (const MergedGame& g : lib.games)
        if (g.game.key == key) return &g;
    return nullptr;
}
}  // namespace

TEST_CASE("library_merge: gleicher Schlüssel auf zwei Geräten erscheint einmal, erstes Gerät gewinnt") {
    const std::vector<DeviceIndex> in{dev("usb", {nes("Zelda.nes", "Zelda USB"), nes("Metroid.nes", "Metroid")}),
                                      dev("sd", {nes("Zelda.nes", "Zelda SD"), nes("Kirby.nes", "Kirby")})};
    const MergedLibrary lib = mergeIndexes(in);
    CHECK(lib.devices == std::vector<std::string>{"usb", "sd"});
    REQUIRE(lib.games.size() == 3);
    const MergedGame* zelda = find(lib, "nes/Zelda.nes");
    REQUIRE(zelda != nullptr);
    CHECK(zelda->device == 0);
    CHECK(zelda->game.title == "Zelda USB");
    CHECK(zelda->alsoOn == std::vector<u8>{1});
    const MergedGame* kirby = find(lib, "nes/Kirby.nes");
    REQUIRE(kirby != nullptr);
    CHECK(kirby->device == 1);
    CHECK(kirby->alsoOn.empty());
    // Sortiert wie der Scanner: Konsole, dann Titel.
    CHECK(lib.games[0].game.title == "Kirby");
    CHECK(lib.games[1].game.title == "Metroid");
}

TEST_CASE("library_merge: Reihenfolge der Quellen entscheidet den Gewinner") {
    const DeviceIndex usb = dev("usb", {nes("Zelda.nes", "Zelda USB")});
    const DeviceIndex sd = dev("sd", {nes("Zelda.nes", "Zelda SD")});
    const std::vector<DeviceIndex> usbFirst{usb, sd}, sdFirst{sd, usb};
    CHECK(mergeIndexes(usbFirst).games[0].game.title == "Zelda USB");
    const MergedLibrary lib = mergeIndexes(sdFirst);
    CHECK(lib.games[0].game.title == "Zelda SD");
    CHECK(lib.games[0].device == 0);
    CHECK(lib.devices[lib.games[0].alsoOn[0]] == "usb");
}

TEST_CASE("library_merge: leeres Gerät und keine Geräte") {
    const std::vector<DeviceIndex> in{dev("usb", {}), dev("sd", {nes("Kirby.nes", "Kirby")})};
    const MergedLibrary lib = mergeIndexes(in);
    CHECK(lib.devices.size() == 2);
    REQUIRE(lib.games.size() == 1);
    CHECK(lib.games[0].device == 1);
    CHECK(mergeIndexes({}).games.empty());
}

TEST_CASE("library_merge: deviceLabel") {
    CHECK(deviceLabel("sd") == "SD");
    CHECK(deviceLabel("usb") == "USB 1");
    CHECK(deviceLabel("usb2") == "USB 2");
    CHECK(deviceLabel("usb8") == "USB 8");
}

TEST_CASE("library_merge: coverRoot je Gerät") {
    const std::string w = coverRoot("usb2", true, "sd:/newlauncher");
    CHECK(w == "usb2:");
    CHECK(coverSrcPath(w, "wii/RMCP01") == "usb2:/newlauncher/covers_src/wii/RMCP01.png");
    const std::string ro = coverRoot("usb2", false, "sd:/newlauncher");
    CHECK(ro == "sd:/newlauncher/cache/usb2");
    CHECK(coverTexPath(ro, "wii/RMCP01") == "sd:/newlauncher/cache/usb2/newlauncher/cache/covers/wii/RMCP01.tex");
    CHECK(deviceCacheDir("sd", true, "usb:/newlauncher") == "sd:/newlauncher/cache");
    CHECK(deviceCacheDir("usb2", false, "sd:/newlauncher") == "sd:/newlauncher/cache/usb2/newlauncher/cache");
}

TEST_CASE("library_merge: scanDevices scannt jedes Gerät für sich") {
    test::MemFileSystem fs;
    fs.addFile("usb:/roms/nes/Zelda.nes", std::vector<u8>(16, 0));
    fs.addFile("sd:/roms/nes/Zelda.nes", std::vector<u8>(16, 0));
    fs.addFile("sd:/roms/nes/Kirby.nes", std::vector<u8>(16, 0));
    const std::vector<std::string> devices{"usb", "usb2", "sd"};  // usb2 fehlt: wird übersprungen
    auto r = scanDevices(fs, devices, defaultScanRules(), {}, nullptr);
    REQUIRE(r.isOk());
    const MultiScan& m = r.value();
    REQUIRE(m.indexes.size() == 2);
    CHECK(m.indexes[0].device == "usb");
    CHECK(m.indexes[0].index.games.size() == 1);
    CHECK(m.indexes[1].device == "sd");
    CHECK(m.indexes[1].index.games.size() == 2);
    CHECK(m.stats.added == 3);

    // Vorheriger Index je Gerät: nichts neu.
    auto again = scanDevices(fs, devices, defaultScanRules(), m.indexes, nullptr);
    REQUIRE(again.isOk());
    CHECK(again.value().stats.added == 0);

    const std::vector<std::string> none{"usb3"};
    auto missing = scanDevices(fs, none, defaultScanRules(), {}, nullptr);
    REQUIRE_FALSE(missing.isOk());
    CHECK(missing.error().code == ErrorCode::NotMounted);
}

TEST_CASE("coverRootsFor: SD bevorzugt, Rückfall auf die Gerätewurzeln (M24)") {
    const std::vector<std::string> devs = {"usb", "sd", "usb2"};
    const bool writable[] = {true, true, false};
    const auto sd = coverRootsFor(devs, writable, "sd:/newlauncher", CoverStorage::Sd, true);
    CHECK(sd == std::vector<std::string>{"sd:", "sd:", "sd:"});
    const auto noSd = coverRootsFor(devs, writable, "sd:/newlauncher", CoverStorage::Sd, false);
    CHECK(noSd == std::vector<std::string>{"usb:", "sd:", "sd:/newlauncher/cache/usb2"});
    const auto dev = coverRootsFor(devs, writable, "sd:/newlauncher", CoverStorage::Device, true);
    CHECK(dev == noSd);
    const bool shortFlags[] = {true};  // fehlende Angabe gilt als nicht beschreibbar
    CHECK(coverRootsFor(devs, shortFlags, "d", CoverStorage::Device, false)[2] == "d/cache/usb2");
}

TEST_CASE("uniqueRoots: Reihenfolge bleibt, Doppelte fallen weg (M24)") {
    const std::vector<std::string> roots = {"sd:", "usb:", "sd:", "usb:", "x"};
    CHECK(uniqueRoots(roots) == std::vector<std::string>{"sd:", "usb:", "x"});
}
