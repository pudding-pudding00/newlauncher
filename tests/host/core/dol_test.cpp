#include "core/dol.hpp"
#include "core/launch_layout.hpp"
#include "support/dol_images.hpp"
#include <doctest/doctest.h>
using namespace nl;

namespace {
Result<DolHeader> parse(const std::vector<u8>& img) { return parseDol(img, img.size()); }
}  // namespace

TEST_CASE("parseDol liest Sektionen, BSS und Einsprung") {
    const auto img = test::dolImage({{0x100, 0x80004000, 0x200}}, {{0, 0x80010000, 0x40}}, 0x80004000, 0x80020000, 0x1000);
    auto r = parse(img);
    REQUIRE(r.isOk());
    CHECK(r.value().sections[0].address == 0x80004000);
    CHECK(r.value().sections[0].size == 0x200);
    CHECK(r.value().sections[7].address == 0x80010000);
    CHECK(r.value().bssAddress == 0x80020000);
    CHECK(r.value().bssSize == 0x1000);
    CHECK(r.value().entry == 0x80004000);
}

TEST_CASE("Datei kürzer als Header oder Sektion endet hinter Dateiende → InvalidData") {
    const auto img = test::dolImage({{0x100, 0x80004000, 0x200}}, {}, 0x80004000);
    auto shortHead = parseDol(std::span<const u8>(img.data(), 0x80), 0x80);
    REQUIRE_FALSE(shortHead.isOk());
    CHECK(shortHead.error().code == ErrorCode::InvalidData);
    auto truncated = parseDol(img, 0x200);  // Sektion endet bei 0x300
    REQUIRE_FALSE(truncated.isOk());
    CHECK(truncated.error().code == ErrorCode::InvalidData);
}

TEST_CASE("keine Text-Sektion oder Einsprung außerhalb aller Text-Sektionen → InvalidData") {
    CHECK_FALSE(parse(test::dolImage({}, {{0x100, 0x80010000, 0x40}}, 0x80010000)).isOk());
    CHECK_FALSE(parse(test::dolImage({{0x100, 0x80004000, 0x200}}, {}, 0x80004200)).isOk());  // genau dahinter
    CHECK_FALSE(parse(test::dolImage({{0x100, 0x80004000, 0x200}}, {{0, 0x80010000, 0x40}}, 0x80010000)).isOk());
    std::vector<u8> garbage(0x400, 0xAB);
    CHECK_FALSE(parse(garbage).isOk());
}

TEST_CASE("Ziel außerhalb MEM1/MEM2 oder überlappt den reservierten Bereich → InvalidData") {
    CHECK_FALSE(parse(test::dolImage({{0x100, 0x70000000, 0x200}}, {}, 0x70000000)).isOk());
    CHECK_FALSE(parse(test::dolImage({{0x100, 0x80000000, 0x200}}, {}, 0x80000000)).isOk());  // vor kMem1Begin
    const u32 entry = 0x80004000;
    CHECK_FALSE(parse(test::dolImage({{0x100, entry, 0x200}}, {{0, 0x92900000, 0x40}}, entry)).isOk());
    CHECK_FALSE(parse(test::dolImage({{0x100, entry, 0x200}}, {{0, 0x933000F0, 0x40}}, entry)).isOk());
    CHECK_FALSE(parse(test::dolImage({{0x100, entry, 0x200}}, {{0, kDolBufferAddr - 0x20, 0x40}}, entry)).isOk());
    CHECK_FALSE(parse(test::dolImage({{0x100, 0x817FFF00, 0x200}}, {}, 0x817FFF00)).isOk());  // über MEM1-Ende
    CHECK(parse(test::dolImage({{0x100, entry, 0x200}}, {{0, 0x90100000, 0x40}}, entry)).isOk());
    // BSS zählt ebenfalls als Ziel
    CHECK_FALSE(parse(test::dolImage({{0x100, entry, 0x200}}, {}, entry, 0x93300000, 0x100)).isOk());
}

TEST_CASE("leere Sektionen (size 0) werden ignoriert, egal welche Adresse") {
    auto img = test::dolImage({{0x100, 0x80004000, 0x200}, {0, 0x12345678, 0}}, {{0, 0xFFFFFFF0, 0}}, 0x80004000);
    test::putBe32(img, 0x04, 0xFFFFFF00);  // Offset der leeren Sektion zeigt ins Nichts
    CHECK(parse(img).isOk());
}

TEST_CASE("Speicherlayout: Puffer, Booter und argv liegen hintereinander im reservierten Bereich") {
    static_assert(kDolBufferAddr < kBooterAddr && kBooterAddr < kArgsAddr && kArgsAddr < kReservedEnd);
    CHECK(kMaxDolBytes == kBooterAddr - kDolBufferAddr);
    CHECK(kMaxDolBytes == 11u * 1024 * 1024);
}
