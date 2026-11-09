#include "gfx/shapes.hpp"
#include <doctest/doctest.h>
using namespace nl;
using namespace nl::gfx;

namespace {
const AtlasShapes kShapes{UvRect{0.0f, 0.0f, 0.01f, 0.01f}, UvRect{0.5f, 0.5f, 0.6f, 0.6f}};
const Color kC{10, 20, 30, 255};

bool insideRect(const DrawList& dl, Rect r) {
    for (const Vertex& v : dl.vertices()) {
        if (v.x < r.x - 0.001f || v.x > r.x + r.w + 0.001f) return false;
        if (v.y < r.y - 0.001f || v.y > r.y + r.h + 0.001f) return false;
    }
    return true;
}
}  // namespace

TEST_CASE("fillRect erzeugt ein Quad mit den UVs des weißen Blocks") {
    DrawList dl(16, 4);
    fillRect(dl, kShapes, Rect{1, 2, 3, 4}, kC);
    REQUIRE(dl.quadCount() == 1);
    CHECK(dl.vertices()[0].u == doctest::Approx(0.0f));
    CHECK(dl.vertices()[2].u == doctest::Approx(0.01f));
}

TEST_CASE("roundRect erzeugt 7 Quads und bleibt in den Grenzen des Rechtecks") {
    DrawList dl(16, 4);
    Rect r{100, 50, 88, 123};
    roundRect(dl, kShapes, r, 10, kC);
    CHECK(dl.quadCount() == 7);
    CHECK(insideRect(dl, r));
}

TEST_CASE("roundRect mit radius > min(w,h)/2 klemmt den Radius") {
    DrawList dl(16, 4);
    Rect r{0, 0, 40, 20};
    roundRect(dl, kShapes, r, 50, kC);
    CHECK(dl.quadCount() == 7);
    CHECK(insideRect(dl, r));
    // Eckquad links oben hat die Größe des geklemmten Radius (10).
    bool found = false;
    for (std::size_t i = 0; i < dl.vertices().size(); i += 4) {
        const Vertex* q = &dl.vertices()[i];
        if (q[0].x == 0 && q[0].y == 0 && q[2].x == doctest::Approx(10) && q[2].y == doctest::Approx(10)) found = true;
    }
    CHECK(found);
}

TEST_CASE("roundRect mit radius 0: 1 Quad; leeres Rect: 0 Quads") {
    DrawList dl(16, 4);
    roundRect(dl, kShapes, Rect{0, 0, 10, 10}, 0, kC);
    CHECK(dl.quadCount() == 1);
    dl.clear();
    roundRect(dl, kShapes, Rect{0, 0, 0, 10}, 4, kC);
    roundRect(dl, kShapes, Rect{0, 0, 10, -1}, 4, kC);
    CHECK(dl.quadCount() == 0);
}

TEST_CASE("circle zeichnet ein Quad um den Mittelpunkt mit den Kreis-UVs") {
    DrawList dl(16, 4);
    circle(dl, kShapes, 50, 60, 10, kC);
    REQUIRE(dl.quadCount() == 1);
    CHECK(dl.vertices()[0].x == 40); CHECK(dl.vertices()[0].y == 50);
    CHECK(dl.vertices()[2].x == 60); CHECK(dl.vertices()[2].y == 70);
    CHECK(dl.vertices()[0].u == doctest::Approx(0.5f));
}
