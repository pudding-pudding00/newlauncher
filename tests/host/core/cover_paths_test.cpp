#include "core/cover_paths.hpp"
#include <doctest/doctest.h>
using namespace nl;

namespace {
Game g(Console c, const char* key) { return Game{c, key, "t", 0, 0}; }
}  // namespace

TEST_CASE("coverStem") {
    CHECK(coverStem(g(Console::Wii, "RMCP01")) == "wii/RMCP01");
    CHECK(coverStem(g(Console::Gc, "GM8P01")) == "gc/GM8P01");
    CHECK(coverStem(g(Console::Nes, "nes/Super Mario Bros. (World).nes")) == "nes/Super Mario Bros. (World)");
    CHECK(coverStem(g(Console::N64, "n64/A.B.z64")) == "n64/A.B");
    CHECK(coverStem(g(Console::Gb, "gb/NoExt")) == "gb/NoExt");
}

TEST_CASE("Cover-Pfade") {
    CHECK(coverSrcPath("sd:", "nes/X") == "sd:/newlauncher/covers_src/nes/X.png");
    CHECK(coverTexPath("sd:", "nes/X") == "sd:/newlauncher/cache/covers/nes/X.tex");
    CHECK(coverTexPath("usb:", "wii/RMCP01") == "usb:/newlauncher/cache/covers/wii/RMCP01.tex");
}
