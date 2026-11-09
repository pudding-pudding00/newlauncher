#include "gfx/font.hpp"
#include <doctest/doctest.h>
#include <string_view>
using namespace nl;
using namespace nl::gfx;

TEST_CASE("glyphIndex") {
    CHECK(glyphIndex(' ') == 0);
    CHECK(glyphIndex('~') == 94);
    CHECK(glyphIndex(0xA0) == 95);
    CHECK(glyphIndex(0xFF) == 190);
    CHECK(glyphIndex(0x2026) == 191);
    CHECK(glyphIndex(0x3042) == glyphIndex('?'));
    CHECK(glyphIndex(0x7F) == glyphIndex('?'));
    CHECK(glyphIndex(0) == glyphIndex('?'));
    CHECK(glyphIndex(0x1F600) == glyphIndex('?'));
}

TEST_CASE("decodeUtf8: ASCII, ä, …") {
    std::string_view s = "A\xC3\xA4\xE2\x80\xA6";
    std::size_t pos = 0;
    CHECK(decodeUtf8(s, pos) == 'A');
    CHECK(pos == 1);
    CHECK(decodeUtf8(s, pos) == 0xE4);
    CHECK(pos == 3);
    CHECK(decodeUtf8(s, pos) == 0x2026);
    CHECK(pos == 6);
}

TEST_CASE("decodeUtf8: vierbyte-Folge wird erkannt") {
    std::string_view s = "\xF0\x9F\x98\x80";
    std::size_t pos = 0;
    CHECK(decodeUtf8(s, pos) == 0x1F600);
    CHECK(pos == 4);
}

TEST_CASE("decodeUtf8: ungültige Folgen ergeben '?' und lesen nie hinter das Ende") {
    const std::string_view cases[] = {
        std::string_view("\xC3", 1),         std::string_view("\xFF", 1),
        std::string_view("\x80", 1),         std::string_view("\xE2\x80", 2),
        std::string_view("\xC0\xAF", 2),     std::string_view("\xED\xA0\x80", 3),
        std::string_view("\xF4\x90\x80\x80", 4), std::string_view("a\xC3(b", 4),
    };
    for (std::string_view s : cases) {
        std::size_t pos = 0;
        int steps = 0;
        bool sawQuestion = false;
        while (pos < s.size()) {
            const std::size_t before = pos;
            const u32 cp = decodeUtf8(s, pos);
            CHECK(pos > before);
            CHECK(pos <= s.size());
            if (cp == '?') sawQuestion = true;
            REQUIRE(++steps <= 8);
        }
        CHECK(pos == s.size());
        CHECK(sawQuestion);
    }
}

TEST_CASE("decodeUtf8 am Ende liefert '?' und rückt vor") {
    std::size_t pos = 3;
    CHECK(decodeUtf8("abc", pos) == '?');
    CHECK(pos == 4);
}
