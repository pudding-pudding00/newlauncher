#include "core/crc32.hpp"
#include <doctest/doctest.h>
#include <string_view>
using namespace nl;

namespace {
std::span<const u8> bytes(std::string_view s) { return {reinterpret_cast<const u8*>(s.data()), s.size()}; }
}  // namespace

TEST_CASE("crc32: Prüfwert wie zlib") {
    CHECK(crc32(bytes("123456789")) == 0xCBF43926u);
    CHECK(crc32(bytes("")) == 0u);
}

TEST_CASE("crc32: fortsetzbar über Teilstücke") {
    const u32 part = crc32(bytes("12345"));
    CHECK(crc32(bytes("6789"), part) == 0xCBF43926u);
}
