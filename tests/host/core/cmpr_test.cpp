#include "core/cmpr.hpp"
#include <doctest/doctest.h>
#include <vector>
using namespace nl;

namespace {
std::vector<u8> image(u16 w, u16 h, u32 rgba) {
    std::vector<u8> px(std::size_t(w) * h * 4);
    for (std::size_t i = 0; i < px.size(); i += 4) {
        px[i] = u8(rgba >> 24); px[i + 1] = u8(rgba >> 16); px[i + 2] = u8(rgba >> 8); px[i + 3] = u8(rgba);
    }
    return px;
}
void setPx(std::vector<u8>& px, u16 w, int x, int y, u32 rgba) {
    const std::size_t i = (std::size_t(y) * w + x) * 4;
    px[i] = u8(rgba >> 24); px[i + 1] = u8(rgba >> 16); px[i + 2] = u8(rgba >> 8); px[i + 3] = u8(rgba);
}
u16 be16(const std::vector<u8>& d, std::size_t at) { return u16(d[at] << 8 | d[at + 1]); }
// Farbe, auf die Pixel (x,y) im Unterblock zeigt (nur 4-Farben-Modus, Index 0 oder 1).
u16 colorOf(const std::vector<u8>& d, std::size_t block, int x, int y) {
    const u16 c0 = be16(d, block), c1 = be16(d, block + 2);
    const int idx = (d[block + 4 + y] >> (6 - 2 * x)) & 3;
    REQUIRE(idx <= 1);
    return idx == 0 ? c0 : c1;
}
}  // namespace

TEST_CASE("CMPR: einfarbiger Block") {
    const auto out = encodeCmpr(image(8, 8, 0xFF0000FF), 8, 8);
    REQUIRE(out.size() == 32);
    for (std::size_t b = 0; b < 32; b += 8) {
        CHECK(be16(out, b) > be16(out, b + 2));  // 4-Farben-Modus
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) CHECK(colorOf(out, b, x, y) == 0xF800);
    }
}

TEST_CASE("CMPR: Unterblock-Reihenfolge") {
    auto px = image(8, 8, 0);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            u32 c = x < 4 ? (y < 4 ? 0xFF0000FF : 0x0000FFFF) : (y < 4 ? 0x00FF00FF : 0xFFFFFFFF);
            setPx(px, 8, x, y, c);
        }
    const auto out = encodeCmpr(px, 8, 8);
    REQUIRE(out.size() == 32);
    CHECK(colorOf(out, 0, 0, 0) == 0xF800);   // links oben rot
    CHECK(colorOf(out, 8, 0, 0) == 0x07E0);   // rechts oben grün
    CHECK(colorOf(out, 16, 0, 0) == 0x001F);  // links unten blau
    CHECK(colorOf(out, 24, 0, 0) == 0xFFFF);  // rechts unten weiß
}

TEST_CASE("CMPR: Transparenz erzwingt 3-Farben-Modus") {
    auto px = image(8, 8, 0x808080FF);
    setPx(px, 8, 1, 2, 0x00000000);
    const auto out = encodeCmpr(px, 8, 8);
    REQUIRE(out.size() == 32);
    CHECK(be16(out, 0) <= be16(out, 2));
    CHECK(((out[4 + 2] >> (6 - 2 * 1)) & 3) == 3);
    CHECK(((out[4 + 0] >> 6) & 3) != 3);
    CHECK(be16(out, 8) > be16(out, 10));  // Nachbarblock ohne Transparenz bleibt 4-Farben
}

TEST_CASE("CMPR: Kachelreihenfolge 16x8") {
    auto px = image(16, 8, 0xFF0000FF);
    for (int y = 0; y < 8; ++y)
        for (int x = 8; x < 16; ++x) setPx(px, 16, x, y, 0x0000FFFF);
    const auto out = encodeCmpr(px, 16, 8);
    REQUIRE(out.size() == 64);
    CHECK(colorOf(out, 0, 0, 0) == 0xF800);
    CHECK(colorOf(out, 32, 0, 0) == 0x001F);
}

TEST_CASE("CMPR: ungültige Größe") {
    CHECK(encodeCmpr(image(12, 8, 0), 12, 8).empty());
    CHECK(encodeCmpr(image(8, 8, 0), 16, 8).empty());  // Puffer zu klein
}
