#include "core/rgb5a3.hpp"

#include <doctest/doctest.h>

using namespace nl;

namespace {
u16 at(const std::vector<u8>& t, std::size_t i) { return static_cast<u16>((t[i * 2] << 8) | t[i * 2 + 1]); }
}  // namespace

TEST_CASE("encodeRgb5a3: deckend → 1rrrrrgggggbbbbb, halbtransparent → 0aaarrrrggggbbbb") {
    const std::vector<u8> rgba{0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x80};
    const Rgb5a3Image img = encodeRgb5a3(rgba, 2, 1);
    CHECK(img.width == 4);   // auf 4×4-Kacheln aufgefüllt
    CHECK(img.height == 4);
    REQUIRE(img.texels.size() == 4 * 4 * 2);
    CHECK(at(img.texels, 0) == 0xFC00);
    CHECK(at(img.texels, 1) == 0x4F00);
    CHECK(at(img.texels, 2) == 0x0000);  // Auffüllung durchsichtig
}

TEST_CASE("encodeRgb5a3: Kachelreihenfolge 4×4, zeilenweise in der Kachel") {
    std::vector<u8> rgba(8 * 4 * 4, 0);
    // Pixel (4,0) liegt in der zweiten Kachel an Stelle 0, Pixel (1,1) in der ersten an Stelle 5.
    rgba[(0 * 8 + 4) * 4 + 3] = 0xFF;
    rgba[(0 * 8 + 4) * 4 + 2] = 0xFF;
    rgba[(1 * 8 + 1) * 4 + 3] = 0xFF;
    rgba[(1 * 8 + 1) * 4 + 1] = 0xFF;
    const Rgb5a3Image img = encodeRgb5a3(rgba, 8, 4);
    CHECK(at(img.texels, 16) == 0x801F);  // blau
    CHECK(at(img.texels, 5) == 0x83E0);   // grün
}

TEST_CASE("encodeRgb5a3: 640×480 bleibt unter 1,2 MiB") {
    std::vector<u8> rgba(640u * 480u * 4u, 0xFF);
    const Rgb5a3Image img = encodeRgb5a3(rgba, 640, 480);
    CHECK(img.texels.size() <= kMaxBackgroundTextureBytes);
    CHECK(kMaxBackgroundTextureBytes == 1258291);  // 1,2 MiB
}

TEST_CASE("encodeRgb5a3: zu kleiner Puffer → leer") {
    CHECK(encodeRgb5a3(std::vector<u8>(4), 2, 2).texels.empty());
}

TEST_CASE("pngSize: IHDR lesen, kaputt → nullopt") {
    std::vector<u8> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I', 'H', 'D', 'R',
                        0, 0, 0x02, 0x80, 0, 0, 0x01, 0xE0, 8, 6, 0, 0, 0};
    const auto s = pngSize(png);
    REQUIRE(s);
    CHECK(s->first == 640);
    CHECK(s->second == 480);
    png[1] = 'X';
    CHECK_FALSE(pngSize(png));
    CHECK_FALSE(pngSize(std::vector<u8>(10)));
}

TEST_CASE("backgroundFits: höchstens 640×480") {
    CHECK(backgroundFits(640, 480));
    CHECK(backgroundFits(1, 1));
    CHECK_FALSE(backgroundFits(641, 480));
    CHECK_FALSE(backgroundFits(640, 481));
    CHECK_FALSE(backgroundFits(0, 10));
}
