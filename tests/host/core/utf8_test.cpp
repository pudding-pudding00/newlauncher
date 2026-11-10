#include "core/utf8.hpp"
#include <doctest/doctest.h>
#include <string>
using namespace nl;

TEST_CASE("appendUtf8") {
    std::string s;
    appendUtf8(s, 0x41);
    appendUtf8(s, 0xE4);
    appendUtf8(s, 0x20AC);
    appendUtf8(s, 0x1F3AE);
    CHECK(s == "A\xC3\xA4\xE2\x82\xAC\xF0\x9F\x8E\xAE");
    std::string bad;
    appendUtf8(bad, 0xD800);
    appendUtf8(bad, 0x110000);
    CHECK(bad == "\xEF\xBF\xBD\xEF\xBF\xBD");
}
