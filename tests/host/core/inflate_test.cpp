#include "core/inflate.hpp"
#include "core/crc32.hpp"
#include "support/deflate_samples.hpp"
#include <doctest/doctest.h>
#include <vector>
using namespace nl;
using namespace nl::test;

namespace {
template <std::size_t N>
std::span<const u8> span(const unsigned char (&a)[N]) { return {a, N}; }

void checkSample(std::span<const u8> packed, std::size_t size, u32 crc) {
    auto r = inflateRaw(packed, size);
    REQUIRE(r.isOk());
    CHECK(r.value().size() == size);
    CHECK(crc32(r.value()) == crc);
}
}  // namespace

TEST_CASE("inflate: gespeicherter Block") { checkSample(span(kStoredData), kStoredSize, kStoredCrc); }
TEST_CASE("inflate: feste Huffman-Codes") { checkSample(span(kFixedData), kFixedSize, kFixedCrc); }
TEST_CASE("inflate: dynamische Huffman-Codes") { checkSample(span(kDynamicData), kDynamicSize, kDynamicCrc); }
TEST_CASE("inflate: mehrere Blöcke, weite Rückverweise") {
    checkSample(span(kMultiBlockData), kMultiBlockSize, kMultiBlockCrc);
}

TEST_CASE("inflate: falsche erwartete Länge ist ein Fehler") {
    CHECK(!inflateRaw(span(kDynamicData), kDynamicSize - 1).isOk());
    CHECK(!inflateRaw(span(kDynamicData), kDynamicSize + 1).isOk());
}

TEST_CASE("inflate: abgeschnittene Eingabe") {
    const auto all = span(kDynamicData);
    auto r = inflateRaw(all.first(all.size() / 2), kDynamicSize);
    REQUIRE(!r.isOk());
    CHECK(r.error().code == ErrorCode::InvalidData);
    CHECK(!inflateRaw(span(kStoredData).first(20), kStoredSize).isOk());
    CHECK(!inflateRaw({}, 1).isOk());
}

TEST_CASE("inflate: ungültiger Blocktyp 3") {
    const u8 bad[] = {0x07, 0x00};
    CHECK(!inflateRaw(bad, 1).isOk());
}

TEST_CASE("inflate: Rückverweis vor den Anfang") {
    // Fester Block, erstes Symbol Länge 3 (Code 257) mit Distanz 1 – es gibt noch kein Byte davor.
    const u8 bad[] = {0x03, 0x02};
    CHECK(!inflateRaw(bad, 3).isOk());
}
