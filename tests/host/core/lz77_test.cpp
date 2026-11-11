#include "core/crc32.hpp"
#include "core/lz77.hpp"
#include "support/lz77_samples.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace nl;

namespace {
template <std::size_t N>
std::span<const u8> view(const unsigned char (&a)[N]) { return {a, N}; }

void checkSample(std::span<const u8> packed, std::size_t size, u32 crc) {
    auto r = lz77Decompress(packed);
    REQUIRE(r.isOk());
    CHECK(r.value().size() == size);
    CHECK(crc32(r.value()) == crc);
}
}  // namespace

TEST_CASE("lz77: Proben ergeben Länge und CRC") {
    using namespace nl::test;
    checkSample(view(kLz10TextData), kLz10TextSize, kLz10TextCrc);
    checkSample(view(kLz11LongData), kLz11LongSize, kLz11LongCrc);
    checkSample(view(kLz11ShortData), kLz11ShortSize, kLz11ShortCrc);
    checkSample(view(kLz10MagicData), kLz10MagicSize, kLz10MagicCrc);
    checkSample(view(kLz11ExtData), kLz11ExtSize, kLz11ExtCrc);
}

TEST_CASE("lz77: isLz77 erkennt Typbyte und Präfix") {
    const std::vector<u8> t10{0x10, 4, 0, 0, 0};
    const std::vector<u8> t11{0x11, 4, 0, 0, 0};
    const std::vector<u8> magic{'L', 'Z', '7', '7', 0x10, 4, 0, 0};
    const std::vector<u8> magicBad{'L', 'Z', '7', '7', 0x24, 4, 0, 0};
    const std::vector<u8> other{0x24, 4, 0, 0, 0};
    const std::vector<u8> tiny{0x10, 4, 0};
    CHECK(isLz77(t10));
    CHECK(isLz77(t11));
    CHECK(isLz77(magic));
    CHECK_FALSE(isLz77(magicBad));
    CHECK_FALSE(isLz77(other));
    CHECK_FALSE(isLz77(tiny));
}

TEST_CASE("lz77: Rückverweis vor den Anfang ist ungültig") {
    const std::vector<u8> in{0x10, 4, 0, 0, 0x80, 0x00, 0x05};
    auto r = lz77Decompress(in);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().code == ErrorCode::InvalidData);
}

TEST_CASE("lz77: abgeschnittene Eingabe ist ungültig") {
    using namespace nl::test;
    const std::span<const u8> full = view(kLz10TextData);
    auto r = lz77Decompress(full.first(full.size() / 2));
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().code == ErrorCode::InvalidData);
}

TEST_CASE("lz77: Kopfgröße über maxOut wird abgelehnt") {
    const std::vector<u8> in{0x10, 0x20, 0, 0, 0x00, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
    auto r = lz77Decompress(in, 16);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().code == ErrorCode::InvalidData);
}

TEST_CASE("lz77: Rückverweis über das Ende wird abgeschnitten") {
    // Größe 5: Literal 'a', dann Rückverweis Länge 18, Distanz 1 → genau 5 Bytes "aaaaa".
    const std::vector<u8> in{0x10, 5, 0, 0, 0x40, 'a', 0xF0, 0x00};
    auto r = lz77Decompress(in);
    REQUIRE(r.isOk());
    CHECK(r.value() == std::vector<u8>{'a', 'a', 'a', 'a', 'a'});
}

TEST_CASE("lz77: Größe 0 auch im erweiterten Kopf ist ungültig") {
    const std::vector<u8> in{0x11, 0, 0, 0, 0, 0, 0, 0, 0x00, 'a'};
    auto r = lz77Decompress(in);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().code == ErrorCode::InvalidData);
}

TEST_CASE("lz77: abgeschnittener Kopf und fremdes Typbyte sind ungültig") {
    const std::vector<u8> shortHead{0x10, 4};
    const std::vector<u8> extCut{0x11, 0, 0, 0, 5, 0};
    const std::vector<u8> wrongType{0x24, 4, 0, 0, 0x00, 'a', 'b', 'c', 'd'};
    CHECK_FALSE(lz77Decompress(shortHead).isOk());
    CHECK_FALSE(lz77Decompress(extCut).isOk());
    CHECK_FALSE(lz77Decompress(wrongType).isOk());
}
