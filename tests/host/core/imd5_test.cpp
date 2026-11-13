#include "core/crc32.hpp"
#include "core/imd5.hpp"
#include "support/lz77_samples.hpp"
#include "support/u8_images.hpp"

#include <doctest/doctest.h>

using namespace nl;

TEST_CASE("imd5: LZ77-0x11 wird entpackt") {
    const std::vector<u8> packed(test::kLz11ShortData, test::kLz11ShortData + sizeof test::kLz11ShortData);
    auto r = unwrapImd5(test::imd5(packed), 1u << 20);
    REQUIRE(r.isOk());
    CHECK(r.value().size() == test::kLz11ShortSize);
    CHECK(crc32(r.value()) == test::kLz11ShortCrc);
}

TEST_CASE("imd5: ohne LZ77 die Bytes ab Offset 32") {
    auto r = unwrapImd5(test::imd5({0x55, 0xAA, 0x38, 0x2D, 7}), 64);
    REQUIRE(r.isOk());
    CHECK(r.value() == std::vector<u8>{0x55, 0xAA, 0x38, 0x2D, 7});
}

TEST_CASE("imd5: maxOut überschritten, falsches Magic, Größe hinter dem Ende → Fehler") {
    CHECK_FALSE(unwrapImd5(test::imd5(std::vector<u8>(100, 0x55)), 99).isOk());
    const std::vector<u8> packed(test::kLz11ShortData, test::kLz11ShortData + sizeof test::kLz11ShortData);
    CHECK_FALSE(unwrapImd5(test::imd5(packed), test::kLz11ShortSize - 1).isOk());
    auto bad = test::imd5({1, 2, 3});
    bad[0] = 'X';
    CHECK(unwrapImd5(bad, 64).error().code == ErrorCode::InvalidData);
    auto longer = test::imd5({1, 2, 3});
    test::putBe32(longer, 4, 1000);
    CHECK(unwrapImd5(longer, 4096).error().code == ErrorCode::InvalidData);
}
