#include "core/gc_banner.hpp"
#include "support/gc_disc_images.hpp"

#include <doctest/doctest.h>

using namespace nl;
using nl::test::BnrText;

TEST_CASE("gc_banner: BNR1 liefert den einen Textblock, Latin-1 wird UTF-8") {
    const auto bnr = test::gcBnr(false, {BnrText{"Kurz", "Mk", "Langer Titel", "Hersteller", "Gr\xFC\xDF" "e"}});
    auto r = parseGcBanner(bnr, 1);
    REQUIRE(r.isOk());
    CHECK(r.value().shortTitle == "Kurz");
    CHECK(r.value().title == "Langer Titel");
    CHECK(r.value().maker == "Hersteller");
    CHECK(r.value().description == "Gr\xC3\xBC\xC3\x9F" "e");
}

TEST_CASE("gc_banner: BNR2 mit langIndex 1 liefert den deutschen Block, außerhalb → Englisch") {
    std::vector<BnrText> blocks(6);
    blocks[0] = BnrText{"EN", "", "English", "", "Text"};
    blocks[1] = BnrText{"DE", "", "Deutsch", "", "Beschreibung"};
    const auto bnr = test::gcBnr(true, blocks);
    auto de = parseGcBanner(bnr, 1);
    REQUIRE(de.isOk());
    CHECK(de.value().title == "Deutsch");
    CHECK(de.value().description == "Beschreibung");
    auto far = parseGcBanner(bnr, 9);
    REQUIRE(far.isOk());
    CHECK(far.value().title == "English");
}

TEST_CASE("gc_banner: Titel ohne Nullbyte bleibt auf seine Feldbreite begrenzt") {
    const auto bnr = test::gcBnr(false, {BnrText{std::string(40, 'A'), std::string(32, 'B'), "", "", ""}});
    auto r = parseGcBanner(bnr, 0);
    REQUIRE(r.isOk());
    CHECK(r.value().shortTitle == std::string(32, 'A'));
}

TEST_CASE("gc_banner: Shift-JIS behält ASCII, Doppelbytezeichen werden zu ?") {
    const auto bnr = test::gcBnr(false, {BnrText{"", "", "AB\x83\x7E" "C", "", ""}});
    auto r = parseGcBanner(bnr, 0, true);
    REQUIRE(r.isOk());
    CHECK(r.value().title == "AB?C");
}

TEST_CASE("gc_banner: falsches Magic oder zu kurz → InvalidData") {
    auto bnr = test::gcBnr(false, {BnrText{}});
    bnr[3] = '3';
    CHECK(parseGcBanner(bnr, 0).error().code == ErrorCode::InvalidData);
    auto two = test::gcBnr(true, std::vector<BnrText>(6));
    two.resize(0x1820 + 0x140);  // BNR2 mit nur einem Block
    CHECK(parseGcBanner(two, 0).error().code == ErrorCode::InvalidData);
}

TEST_CASE("gc_banner: Bilddaten werden übernommen") {
    const auto bnr = test::gcBnr(false, {BnrText{}}, 0x1234, 0x5678);
    auto r = parseGcBanner(bnr, 0);
    REQUIRE(r.isOk());
    CHECK(r.value().rgb5a3[0] == 0x12);
    CHECK(r.value().rgb5a3[3] == 0x78);
}

TEST_CASE("gc_banner: rgb5a3ToRgba für gesetztes und gelöschtes Bit 15") {
    // Kachel 4×4: Pixel 0 = RGB555 (31, 0, 0), Pixel 1 = 3-Bit-Alpha 3, RGB444 (15, 0, 15).
    std::vector<u8> tiled(96 * 32 * 2, 0);
    tiled[0] = 0xFC;
    tiled[1] = 0x00;
    tiled[2] = 0x3F;
    tiled[3] = 0x0F;
    const auto rgba = rgb5a3ToRgba(tiled, 96, 32);
    REQUIRE(rgba.size() == 96u * 32u * 4u);
    CHECK(rgba[0] == 255);
    CHECK(rgba[1] == 0);
    CHECK(rgba[2] == 0);
    CHECK(rgba[3] == 255);
    CHECK(rgba[4] == 255);
    CHECK(rgba[5] == 0);
    CHECK(rgba[6] == 255);
    CHECK(rgba[7] == 0x6D);  // 3 → (3 << 5) | (3 << 2) | (3 >> 1)
    // Zweite Kachel beginnt bei Pixel (4, 0): Byte 32 der Eingabe.
    tiled[32] = 0x80;
    tiled[33] = 0x1F;  // Blau voll
    const auto b = rgb5a3ToRgba(tiled, 96, 32);
    CHECK(b[4 * 4 + 2] == 255);
    CHECK(rgb5a3ToRgba(std::span<const u8>(tiled.data(), 10), 96, 32).empty());
}
