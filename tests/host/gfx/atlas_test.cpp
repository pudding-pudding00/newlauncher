#include "gfx/atlas.hpp"
#include <doctest/doctest.h>
#include <vector>
using namespace nl;
using namespace nl::gfx;

TEST_CASE("ShelfPacker füllt Zeilen, bricht um, meldet voll") {
    ShelfPacker p(64, 32);
    u16 x = 0, y = 0;
    REQUIRE(p.place(30, 14, x, y)); CHECK(x == 0); CHECK(y == 0);
    REQUIRE(p.place(30, 14, x, y)); CHECK(x == 31); CHECK(y == 0);
    REQUIRE(p.place(30, 14, x, y)); CHECK(x == 0); CHECK(y == 15);
    CHECK_FALSE(p.place(100, 10, x, y));
    REQUIRE(p.place(30, 14, x, y)); CHECK(x == 31); CHECK(y == 15);
    CHECK_FALSE(p.place(30, 14, x, y));  // dritte Zeile hätte unten keinen Platz
}

TEST_CASE("ShelfPacker lehnt leere Rechtecke ab") {
    ShelfPacker p(64, 32);
    u16 x = 0, y = 0;
    CHECK_FALSE(p.place(0, 5, x, y));
    CHECK_FALSE(p.place(5, 0, x, y));
}

TEST_CASE("rasterCircle: Mitte 255, Ecke 0, Rand dazwischen") {
    const u16 w = 40;
    std::vector<u8> buf(w * w, 0);
    rasterCircle(buf.data(), w, 4, 4, 32);
    CHECK(buf[(4 + 16) * w + 4 + 16] == 255);
    CHECK(buf[4 * w + 4] == 0);
    CHECK(buf[0] == 0);
    // Pixel (31,31) liegt auf dem Rand unter 45° (Mittelpunkt 20,20, Radius 16): teilweise bedeckt.
    const u8 edge = buf[31 * w + 31];
    CHECK(edge > 0);
    CHECK(edge < 255);
}

TEST_CASE("linearToI8 ordnet 8×4-Kacheln") {
    const u16 w = 16, h = 8;
    std::vector<u8> src(w * h), dst(w * h, 0);
    for (int i = 0; i < w * h; ++i) src[i] = static_cast<u8>(i);
    linearToI8(src.data(), w, h, dst.data());
    CHECK(dst[0] == 0);
    CHECK(dst[7] == 7);
    CHECK(dst[8] == 16);       // zweite Zeile der ersten Kachel
    CHECK(dst[31] == 3 * 16 + 7);
    CHECK(dst[32] == 8);       // erste Zeile der zweiten Kachel
    CHECK(dst[64] == 4 * 16);  // erste Kachel der zweiten Kachelreihe
}
