#include "core/console.hpp"
#include <doctest/doctest.h>
#include <cstring>
using namespace nl;

TEST_CASE("Konsolentabelle laut Spec 5.3") {
    const char* keys[] = {"nes", "snes", "gb", "n64", "gbc", "gc", "gba", "wii", "channel"};
    const int gens[] = {3, 4, 4, 5, 5, 6, 6, 7, 8};
    const bool handheld[] = {false, false, true, false, true, false, true, false, false};
    for (int i = 0; i < kConsoleCount; ++i) {
        const ConsoleInfo& c = consoleInfo(static_cast<Console>(i));
        CHECK(c.id == static_cast<Console>(i));
        CHECK(std::strcmp(c.key, keys[i]) == 0);
        CHECK(c.generation == gens[i]);
        CHECK(c.handheld == handheld[i]);
        CHECK(std::strlen(c.label) > 0);
        CHECK(std::strlen(c.name) > 0);
        if (i > 0) CHECK(consoleInfo(static_cast<Console>(i - 1)).generation <= c.generation);
    }
    CHECK(std::strcmp(consoleInfo(Console::N64).label, "N64") == 0);
}

TEST_CASE("Konsolentabelle: Ordner und Endungen laut Spec 5.3") {
    CHECK(std::strcmp(consoleInfo(Console::Nes).folders[0], "roms/nes") == 0);
    CHECK(std::strcmp(consoleInfo(Console::Gc).folders[0], "games") == 0);
    CHECK(std::strcmp(consoleInfo(Console::Wii).folders[0], "wbfs") == 0);
    CHECK(std::strcmp(consoleInfo(Console::Snes).extensions[0], ".sfc") == 0);
    CHECK(std::strcmp(consoleInfo(Console::Snes).extensions[1], ".smc") == 0);
    CHECK(consoleInfo(Console::Snes).extensions[2] == nullptr);
    CHECK(std::strcmp(consoleInfo(Console::N64).extensions[2], ".v64") == 0);
    CHECK(std::strcmp(consoleInfo(Console::Wii).extensions[1], ".iso") == 0);
}

TEST_CASE("coverAspect: typisches Cover-Format je Konsole (M23, libretro gemessen)") {
    CHECK(coverAspect(Console::Snes) == doctest::Approx(10.0 / 7.0));
    CHECK(coverAspect(Console::N64) == doctest::Approx(10.0 / 7.0));
    CHECK(coverAspect(Console::Gb) == doctest::Approx(1.0));
    CHECK(coverAspect(Console::Gbc) == doctest::Approx(1.0));
    CHECK(coverAspect(Console::Gba) == doctest::Approx(1.0));
    for (Console c : {Console::Nes, Console::Gc, Console::Wii, Console::Channel})
        CHECK(coverAspect(c) == doctest::Approx(5.0 / 7.0));
}
