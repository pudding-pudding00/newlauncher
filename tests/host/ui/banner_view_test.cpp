#include "ui/banner_view.hpp"

#include <doctest/doctest.h>

using namespace nl;
using gfx::Rect;

namespace {
const gfx::AtlasShapes kShapes{gfx::UvRect{0.1f, 0.2f, 0.11f, 0.21f}, gfx::UvRect{0.5f, 0.5f, 0.6f, 0.6f}};

Quad quad(s16 tex) {
    Quad q;
    q.pos = {Vec2{-20, 60}, Vec2{60, 60}, Vec2{-20, 20}, Vec2{60, 20}};
    q.uv = {Vec2{0, 0}, Vec2{1, 0}, Vec2{0, 1}, Vec2{1, 1}};
    q.color = {Color{1, 2, 3, 4}, Color{5, 6, 7, 8}, Color{9, 10, 11, 12}, Color{13, 14, 15, 16}};
    q.texIndex = tex;
    return q;
}
}  // namespace

TEST_CASE("banner_view: Layout 608×456 halb so groß, +Y nach oben, im Uhrzeigersinn") {
    gfx::DrawList dl(16, 8);
    const Quad q[1] = {quad(3)};
    ui::drawBanner(dl, kShapes, Rect{100, 50, 304, 228}, ui::BannerFrame{608, 456, q});
    REQUIRE(dl.quadCount() == 1);
    const auto v = dl.vertices();
    CHECK(v[0].x == doctest::Approx(242));  // 100 + 152 - 20·0.5
    CHECK(v[0].y == doctest::Approx(134));  // 50 + 114 - 60·0.5
    CHECK(v[1].x == doctest::Approx(282));
    CHECK(v[2].y == doctest::Approx(154));  // unten rechts
    CHECK(v[2].color == Color{13, 14, 15, 16});
    CHECK(v[3].color == Color{9, 10, 11, 12});  // unten links
    CHECK(v[2].u == 1.0f);
    const auto b = dl.batches();
    REQUIRE(b.size() == 1);
    CHECK(b[0].texture == gfx::kBannerTextureBase + 3);
    CHECK(b[0].clipped);
    CHECK(b[0].clip.w == 304.0f);
}

TEST_CASE("banner_view: Quad ohne Textur nutzt das weiße Atlas-Feld; danach kein Clip mehr") {
    gfx::DrawList dl(16, 8);
    const Quad q[2] = {quad(-1), quad(gfx::kBannerTextureSlots)};  // zweites: Slot außerhalb → nicht gezeichnet
    ui::drawBanner(dl, kShapes, Rect{0, 0, 608, 456}, ui::BannerFrame{608, 456, q});
    REQUIRE(dl.quadCount() == 1);
    CHECK(dl.batches()[0].texture == 0);
    CHECK(dl.vertices()[0].u == doctest::Approx(0.105f));
    CHECK(dl.vertices()[0].v == doctest::Approx(0.205f));
    dl.quad(0, Rect{0, 0, 1, 1}, gfx::UvRect{0, 0, 1, 1}, Color{255, 255, 255, 255});
    CHECK_FALSE(dl.batches().back().clipped);
}

TEST_CASE("banner_view: Layout ohne Größe zeichnet nichts") {
    gfx::DrawList dl(4, 4);
    const Quad q[1] = {quad(0)};
    ui::drawBanner(dl, kShapes, Rect{0, 0, 100, 75}, ui::BannerFrame{0, 0, q});
    CHECK(dl.quadCount() == 0);
}
