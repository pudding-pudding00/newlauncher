#include "core/tpl.hpp"
#include "support/tpl_images.hpp"

#include <array>

#include <doctest/doctest.h>

using namespace nl;
using test::Rgba;
using test::TplSpec;

namespace {
using Px = std::array<int, 4>;

Px at(const std::vector<u8>& rgba, int w, int x, int y) {
    const u8* p = &rgba[(std::size_t(y) * w + x) * 4];
    return {p[0], p[1], p[2], p[3]};
}

// Testmuster 8×8: Index p = y*8+x.
Rgba intensity(int x, int y) {
    const u8 i = static_cast<u8>((y * 8 + x) * 4);
    return {i, i, i, static_cast<u8>(255 - i)};
}
Rgba color(int x, int y) { return {static_cast<u8>(x * 36), static_cast<u8>(y * 36), 128, 255}; }

// Dekodiert ein einzelnes Bild und liefert die vier Ecken (0,0) (7,0) (0,7) (7,7).
std::array<Px, 4> corners(const TplSpec& s) {
    const auto file = test::tplFile({s});
    auto r = parseTpl(file);
    REQUIRE(r.isOk());
    REQUIRE(r.value().size() == 1);
    const TplImage& img = r.value()[0];
    CHECK(img.fmt == s.fmt);
    CHECK(img.w == s.w);
    CHECK(img.data.size() == texBytes(s.fmt, s.w, s.h));
    const auto rgba = decodeToRgba(img);
    REQUIRE(rgba.size() == std::size_t{s.w} * s.h * 4);
    return {at(rgba, 8, 0, 0), at(rgba, 8, 7, 0), at(rgba, 8, 0, 7), at(rgba, 8, 7, 7)};
}

using C = std::array<Px, 4>;
}  // namespace

TEST_CASE("tpl: Intensitätsformate I4, I8, IA4, IA8") {
    CHECK(corners({TexFmt::I4, 8, 8, intensity}) == C{Px{0, 0, 0, 0}, {17, 17, 17, 17}, {238, 238, 238, 238}, {255, 255, 255, 255}});
    CHECK(corners({TexFmt::I8, 8, 8, intensity}) == C{Px{0, 0, 0, 0}, {28, 28, 28, 28}, {224, 224, 224, 224}, {252, 252, 252, 252}});
    CHECK(corners({TexFmt::IA4, 8, 8, intensity}) == C{Px{0, 0, 0, 255}, {17, 17, 17, 238}, {238, 238, 238, 17}, {255, 255, 255, 0}});
    CHECK(corners({TexFmt::IA8, 8, 8, intensity}) == C{Px{0, 0, 0, 255}, {28, 28, 28, 227}, {224, 224, 224, 31}, {252, 252, 252, 3}});
}

TEST_CASE("tpl: Farbformate RGB565, RGB5A3, RGBA8") {
    CHECK(corners({TexFmt::RGB565, 8, 8, color}) == C{Px{0, 0, 132, 255}, {255, 0, 132, 255}, {0, 255, 132, 255}, {255, 255, 132, 255}});
    auto half = [](int x, int y) {
        Rgba c = color(x, y);
        c.a = y < 4 ? 255 : 0x60;
        return c;
    };
    CHECK(corners({TexFmt::RGB5A3, 8, 8, half}) == C{Px{0, 0, 132, 255}, {255, 0, 132, 255}, {0, 255, 136, 109}, {255, 255, 136, 109}});
    auto fade = [](int x, int y) {
        Rgba c = color(x, y);
        c.a = static_cast<u8>(255 - (y * 8 + x));
        return c;
    };
    CHECK(corners({TexFmt::RGBA8, 8, 8, fade}) == C{Px{0, 0, 128, 255}, {252, 0, 128, 248}, {0, 252, 128, 199}, {252, 252, 128, 192}});
}

TEST_CASE("tpl: Palettenformate C4 (RGB565), C8 (RGB5A3), C14X2 (IA8)") {
    TplSpec c4{TexFmt::C4, 8, 8, {}, [](int x, int y) { return static_cast<u16>((y * 8 + x) % 16); }, TexFmt::RGB565, {}};
    for (u16 k = 0; k < 16; ++k) c4.palette.push_back(static_cast<u16>(k << 11 | 0x3F << 5 | (31 - k)));
    CHECK(corners(c4) == C{Px{0, 255, 255, 255}, {57, 255, 198, 255}, {66, 255, 189, 255}, {123, 255, 132, 255}});

    TplSpec c8{TexFmt::C8, 8, 8, {}, [](int x, int y) { return static_cast<u16>(y * 8 + x); }, TexFmt::RGB5A3, {}};
    for (u16 k = 0; k < 64; ++k) c8.palette.push_back(static_cast<u16>(0x8000 | (k / 2) << 10 | 31));
    CHECK(corners(c8) == C{Px{0, 0, 255, 255}, {24, 0, 255, 255}, {231, 0, 255, 255}, {255, 0, 255, 255}});

    TplSpec c14{TexFmt::C14X2, 8, 8, {}, [](int x, int y) {
                    const int p = y * 8 + x;
                    return static_cast<u16>((p + 1000) | (p == 63 ? 0xC000 : 0));  // obere 2 Bit zählen nicht
                }, TexFmt::IA8, {}};
    for (u16 k = 0; k < 1064; ++k) c14.palette.push_back(static_cast<u16>(0xFF00 | ((k >= 1000 ? k - 1000 : 0) * 4)));
    CHECK(corners(c14) == C{Px{0, 0, 0, 255}, {28, 28, 28, 255}, {224, 224, 224, 255}, {252, 252, 252, 255}});
}

TEST_CASE("tpl: CMPR mit transparentem Unterblock") {
    auto quad = [](int x, int y) {
        if (x < 4 && y < 4) return Rgba{255, 0, 0, 255};
        if (y < 4) return Rgba{0, 255, 0, 255};
        if (x < 4) return Rgba{0, 0, 255, 255};
        return Rgba{0, 0, 0, 0};
    };
    CHECK(corners({TexFmt::CMPR, 8, 8, quad}) == C{Px{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {0, 0, 0, 0}});
}

TEST_CASE("tpl: texBytes rundet auf ganze Kacheln") {
    CHECK(texBytes(TexFmt::I4, 10, 6) == 64u);
    CHECK(texBytes(TexFmt::RGBA8, 10, 6) == 384u);
    CHECK(texBytes(TexFmt::CMPR, 10, 6) == 64u);
    CHECK(texBytes(TexFmt::RGB5A3, 10, 6) == 192u);
    CHECK(texBytes(static_cast<TexFmt>(7), 8, 8) == 0u);
}

TEST_CASE("tpl: Bild, dessen Größe kein Kachelvielfaches ist, dekodiert nur die sichtbaren Pixel") {
    auto r = parseTpl(test::tplFile({{TexFmt::RGBA8, 6, 3, color}}));
    REQUIRE(r.isOk());
    const auto rgba = decodeToRgba(r.value()[0]);
    REQUIRE(rgba.size() == 6u * 3 * 4);
    CHECK(at(rgba, 6, 5, 2) == Px{180, 72, 128, 255});
}

TEST_CASE("tpl: mehrere Bilder in einer Datei") {
    auto r = parseTpl(test::tplFile({{TexFmt::I8, 8, 4, intensity}, {TexFmt::RGB5A3, 4, 4, color}}));
    REQUIRE(r.isOk());
    REQUIRE(r.value().size() == 2);
    CHECK(r.value()[1].fmt == TexFmt::RGB5A3);
    CHECK(r.value()[1].data.size() == 32u);
}

TEST_CASE("tpl: Fehler bei Größe 0/4096, Daten hinter dem Ende, C8 ohne Palette, falschem Magic") {
    auto zero = test::tplFile({{TexFmt::I8, 8, 8, intensity}});
    const u32 header = static_cast<u32>(zero[0x0C] << 24 | zero[0x0D] << 16 | zero[0x0E] << 8 | zero[0x0F]);
    test::putBe16(zero, header + 2, 0);
    CHECK(parseTpl(zero).error().code == ErrorCode::InvalidData);
    auto big = test::tplFile({{TexFmt::I8, 8, 8, intensity}});
    test::putBe16(big, header, 4096);
    CHECK(parseTpl(big).error().code == ErrorCode::InvalidData);
    auto cut = test::tplFile({{TexFmt::RGBA8, 8, 8, color}});
    cut.resize(cut.size() - 1);
    CHECK(parseTpl(cut).error().code == ErrorCode::InvalidData);
    TplSpec c8{TexFmt::C8, 8, 8, {}, [](int, int) { return u16{0}; }, TexFmt::RGB5A3, {0x8000}};
    auto noPal = test::tplFile({c8});
    test::putBe32(noPal, 0x10, 0);  // Palettenkopf-Offset
    CHECK(parseTpl(noPal).error().code == ErrorCode::InvalidData);
    auto fmt = test::tplFile({{TexFmt::I8, 8, 8, intensity}});
    test::putBe32(fmt, header + 4, 7);
    CHECK(parseTpl(fmt).error().code == ErrorCode::InvalidData);
    auto magic = test::tplFile({{TexFmt::I8, 8, 8, intensity}});
    magic[1] = 0;
    CHECK(parseTpl(magic).error().code == ErrorCode::InvalidData);
    CHECK(parseTpl(std::vector<u8>(8, 0)).error().code == ErrorCode::InvalidData);
}
