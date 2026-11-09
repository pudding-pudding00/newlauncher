#include "gfx/draw_list.hpp"
#include <doctest/doctest.h>
using namespace nl;
using namespace nl::gfx;

namespace {
const UvRect kUv{0.0f, 0.0f, 1.0f, 1.0f};
const Color kWhite{255, 255, 255, 255};
}  // namespace

TEST_CASE("quad schreibt 4 Vertices im Uhrzeigersinn ab links oben") {
    DrawList dl(8, 4);
    dl.quad(0, Rect{10, 20, 30, 40}, UvRect{0.1f, 0.2f, 0.3f, 0.4f}, kWhite);
    auto v = dl.vertices();
    REQUIRE(v.size() == 4);
    CHECK(v[0].x == 10); CHECK(v[0].y == 20); CHECK(v[0].u == doctest::Approx(0.1f)); CHECK(v[0].v == doctest::Approx(0.2f));
    CHECK(v[1].x == 40); CHECK(v[1].y == 20); CHECK(v[1].u == doctest::Approx(0.3f));
    CHECK(v[2].x == 40); CHECK(v[2].y == 60); CHECK(v[2].v == doctest::Approx(0.4f));
    CHECK(v[3].x == 10); CHECK(v[3].y == 60);
    CHECK(v[0].color == kWhite);
}

TEST_CASE("gleiche Textur → ein Batch; Texturwechsel → neuer Batch") {
    DrawList dl(8, 4);
    dl.quad(0, Rect{0, 0, 1, 1}, kUv, kWhite);
    dl.quad(0, Rect{0, 0, 1, 1}, kUv, kWhite);
    dl.quad(1, Rect{0, 0, 1, 1}, kUv, kWhite);
    dl.quad(0, Rect{0, 0, 1, 1}, kUv, kWhite);
    auto b = dl.batches();
    REQUIRE(b.size() == 3);
    CHECK(b[0].texture == 0); CHECK(b[0].firstVertex == 0); CHECK(b[0].vertexCount == 8);
    CHECK(b[1].texture == 1); CHECK(b[1].firstVertex == 8); CHECK(b[1].vertexCount == 4);
    CHECK(b[2].texture == 0); CHECK(b[2].firstVertex == 12); CHECK(b[2].vertexCount == 4);
}

TEST_CASE("Überlauf: Quads über maxQuads werden verworfen und gezählt") {
    DrawList dl(2, 4);
    for (int i = 0; i < 5; ++i) dl.quad(0, Rect{0, 0, 1, 1}, kUv, kWhite);
    CHECK(dl.quadCount() == 2);
    CHECK(dl.droppedQuads() == 3);
    CHECK(dl.vertices().size() == 8);
    CHECK(dl.batches()[0].vertexCount == 8);
}

TEST_CASE("Überlauf der Batches verwirft das Quad ebenfalls") {
    DrawList dl(8, 1);
    dl.quad(0, Rect{0, 0, 1, 1}, kUv, kWhite);
    dl.quad(1, Rect{0, 0, 1, 1}, kUv, kWhite);
    CHECK(dl.quadCount() == 1);
    CHECK(dl.droppedQuads() == 1);
    CHECK(dl.batches().size() == 1);
}

TEST_CASE("pushAlpha multipliziert Alpha; popAlpha stellt wieder her") {
    DrawList dl(8, 4);
    dl.pushAlpha(0.5f);
    dl.pushAlpha(0.5f);
    dl.quad(0, Rect{0, 0, 1, 1}, kUv, kWhite);
    dl.popAlpha();
    dl.quad(0, Rect{0, 0, 1, 1}, kUv, kWhite);
    dl.popAlpha();
    dl.quad(0, Rect{0, 0, 1, 1}, kUv, kWhite);
    auto v = dl.vertices();
    CHECK(v[0].color.a >= 63); CHECK(v[0].color.a <= 65);
    CHECK(v[0].color.r == 255);
    CHECK(v[4].color.a >= 127); CHECK(v[4].color.a <= 128);
    CHECK(v[8].color.a == 255);
}

TEST_CASE("popAlpha ohne push ist ein No-op; neuntes pushAlpha wird ignoriert") {
    DrawList dl(8, 4);
    dl.popAlpha();
    dl.quad(0, Rect{0, 0, 1, 1}, kUv, kWhite);
    CHECK(dl.vertices()[0].color.a == 255);
    for (int i = 0; i < 8; ++i) dl.pushAlpha(1.0f);
    dl.pushAlpha(0.0f);  // neunter Eintrag: ignoriert
    dl.quad(0, Rect{0, 0, 1, 1}, kUv, kWhite);
    CHECK(dl.vertices()[4].color.a == 255);
}

TEST_CASE("clear setzt Zähler und dropped zurück") {
    DrawList dl(1, 1);
    dl.pushAlpha(0.5f);
    dl.quad(0, Rect{0, 0, 1, 1}, kUv, kWhite);
    dl.quad(0, Rect{0, 0, 1, 1}, kUv, kWhite);
    dl.clear();
    CHECK(dl.quadCount() == 0);
    CHECK(dl.droppedQuads() == 0);
    CHECK(dl.batches().empty());
    dl.quad(0, Rect{0, 0, 1, 1}, kUv, kWhite);
    CHECK(dl.vertices()[0].color.a == 255);  // clear leert auch den Alpha-Stapel
}
