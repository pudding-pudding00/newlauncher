#include "core/cover_image.hpp"
#include "core/tex_file.hpp"
#include <doctest/doctest.h>
#include <vector>
using namespace nl;

namespace {
std::vector<u8> solid(u32 w, u32 h, u8 r, u8 g, u8 b) {
    std::vector<u8> px(std::size_t(w) * h * 4);
    for (std::size_t i = 0; i < px.size(); i += 4) px[i] = r, px[i + 1] = g, px[i + 2] = b, px[i + 3] = 255;
    return px;
}
const u8* at(const std::vector<u8>& img, int x, int y) { return &img[(std::size_t(y) * kCoverW + x) * 4]; }
}  // namespace

TEST_CASE("fitCover füllt 5:7 ganz") {
    const auto out = fitCover(solid(100, 140, 255, 0, 0), 100, 140);
    REQUIRE(out.size() == std::size_t(kCoverW) * kCoverH * 4);
    for (auto [x, y] : {std::pair{0, 0}, {199, 279}, {100, 140}}) {
        CHECK(at(out, x, y)[0] == 255);
        CHECK(at(out, x, y)[1] == 0);
        CHECK(at(out, x, y)[3] == 255);
    }
}

TEST_CASE("fitCover quadratisch: Ränder oben/unten transparent") {
    const auto out = fitCover(solid(200, 200, 0, 0, 255), 200, 200);
    REQUIRE(out.size() == std::size_t(kCoverW) * kCoverH * 4);
    CHECK(at(out, 100, 0)[3] == 0);
    CHECK(at(out, 100, 39)[3] == 0);
    CHECK(at(out, 100, 240)[3] == 0);
    CHECK(at(out, 100, 279)[3] == 0);
    CHECK(at(out, 100, 40)[3] == 255);
    CHECK(at(out, 100, 140)[3] == 255);
    CHECK(at(out, 100, 239)[3] == 255);
}

TEST_CASE("fitCover breit: Ränder links/rechts bzw. oben/unten") {
    const auto out = fitCover(solid(400, 100, 0, 255, 0), 400, 100);  // → 200×50, mittig
    REQUIRE(!out.empty());
    CHECK(at(out, 100, 10)[3] == 0);
    CHECK(at(out, 100, 140)[3] == 255);
    CHECK(at(out, 100, 140)[1] == 255);
    const auto tall = fitCover(solid(50, 280, 9, 9, 9), 50, 280);  // → 50×280, links/rechts frei
    CHECK(at(tall, 10, 140)[3] == 0);
    CHECK(at(tall, 100, 140)[3] == 255);
}

TEST_CASE("fitCover extreme Formate") {
    const auto one = fitCover(solid(1, 1, 7, 8, 9), 1, 1);
    REQUIRE(!one.empty());
    CHECK(at(one, 0, 40)[3] == 255);  // quadratisch → 200×200 mittig
    CHECK(at(one, 0, 39)[3] == 0);
    CHECK(at(one, 199, 239)[0] == 7);
    const auto wide = fitCover(solid(10000, 10, 1, 2, 3), 10000, 10);
    REQUIRE(!wide.empty());
    bool anyOpaque = false;
    for (int y = 0; y < kCoverH; ++y) anyOpaque |= at(wide, 100, y)[3] == 255;
    CHECK(anyOpaque);
    CHECK(fitCover(solid(1, 1, 0, 0, 0), 0, 5).empty());
    CHECK(fitCover(solid(1, 1, 0, 0, 0), 2, 2).empty());  // Puffer zu klein
}
