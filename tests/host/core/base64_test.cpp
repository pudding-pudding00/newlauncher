#include "core/base64.hpp"

#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <vector>

using namespace nl;

namespace {
std::span<const u8> bytes(std::string_view s) { return {reinterpret_cast<const u8*>(s.data()), s.size()}; }
std::string text(const std::vector<u8>& v) { return std::string(v.begin(), v.end()); }
}  // namespace

TEST_CASE("base64: RFC-4648-Vektoren hin und zurück") {
    const char* plain[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
    const char* coded[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
    for (int i = 0; i < 7; ++i) {
        CHECK(base64Encode(bytes(plain[i])) == coded[i]);
        const auto back = base64Decode(coded[i]);
        REQUIRE(back.has_value());
        CHECK(text(*back) == plain[i]);
    }
}

TEST_CASE("base64: URL-Variante ohne Auffüllung") {
    const u8 raw[] = {0xfb, 0xff};
    CHECK(base64Encode(raw, true) == "-_8");
    const auto back = base64Decode("-_8", true);
    REQUIRE(back.has_value());
    CHECK(*back == std::vector<u8>{0xfb, 0xff});
    CHECK(base64Encode(raw) == "+/8=");
}

TEST_CASE("base64: ungültige Eingaben") {
    CHECK_FALSE(base64Decode("Zm9v!").has_value());
    CHECK_FALSE(base64Decode("Zg=").has_value());
    CHECK_FALSE(base64Decode("Z").has_value());
    CHECK_FALSE(base64Decode("-_8=").has_value());  // URL-Zeichen im Standardalphabet
    CHECK_FALSE(base64Decode("Zg==Zg==").has_value());
}
