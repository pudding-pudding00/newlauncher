#include "core/banner_scene.hpp"
#include "support/brlyt_images.hpp"

#include <doctest/doctest.h>

using namespace nl;
using test::MatSpec;
using test::PaneSpec;

namespace {
Layout parse(const std::vector<PaneSpec>& roots, std::vector<MatSpec> mats = {MatSpec{"m", {0}, {}, {}}}) {
    auto r = parseBrlyt(test::buildBrlyt(608, 456, {"t.tpl"}, mats, roots));
    REQUIRE(r.isOk());
    return r.value();
}
std::vector<Quad> quads(const Layout& l) { return buildQuads(l, initialState(l)); }

void near(Vec2 a, Vec2 b) {
    CHECK(a.x == doctest::Approx(b.x).epsilon(1e-4));
    CHECK(a.y == doctest::Approx(b.y).epsilon(1e-4));
}
}  // namespace

TEST_CASE("banner_scene: Elternskalierung 2 und Kind-Translation (10, 20)") {
    PaneSpec root = test::pane("pan1", "root", 0, 0, 608, 456, 2);
    root.children.push_back(test::pane("pic1", "pic", 10, 20, 40, 20));
    const auto q = quads(parse({root}));
    REQUIRE(q.size() == 1);
    near(q[0].pos[0], {-20, 60});
    near(q[0].pos[1], {60, 60});
    near(q[0].pos[2], {-20, 20});
    near(q[0].pos[3], {60, 20});
    CHECK(q[0].texIndex == 0);
    near(q[0].uv[3], {1, 1});
}

TEST_CASE("banner_scene: Eltern-Alpha 128 × Kind-Alpha 128 → 64") {
    PaneSpec root = test::pane("pan1", "root", 0, 0, 10, 10);
    root.alpha = 128;
    PaneSpec pic = test::pane("pic1", "pic", 0, 0, 4, 4);
    pic.alpha = 128;
    root.children.push_back(pic);
    auto q = quads(parse({root}));
    REQUIRE(q.size() == 1);
    CHECK(q[0].color[0].a == 64);
    root.flags = 0x01;  // Alpha wirkt nicht auf die Kinder
    q = quads(parse({root}));
    CHECK(q[0].color[0].a == 128);
}

TEST_CASE("banner_scene: Ursprung verschiebt das Quad") {
    auto with = [](u8 origin) {
        PaneSpec p = test::pane("pic1", "pic", 0, 0, 40, 20);
        p.origin = origin;
        const auto q = quads(parse({p}));
        REQUIRE(q.size() == 1);
        return q[0];
    };
    const Quad tl = with(0);
    near(tl.pos[0], {0, 0});
    near(tl.pos[3], {40, -20});
    const Quad tr = with(2);
    near(tr.pos[0], {-40, 0});
    near(tr.pos[3], {0, -20});
    const Quad br = with(8);
    near(br.pos[0], {-40, 20});
    near(br.pos[3], {0, 0});
}

TEST_CASE("banner_scene: Drehung um z, Farbe = Vertexfarbe × Materialfarbe") {
    PaneSpec p = test::pane("pic1", "pic", 0, 0, 40, 20);
    p.f[5] = 90;
    p.vtx[0] = Color{255, 128, 0, 255};
    const auto q = quads(parse({p}, {MatSpec{"m", {0}, {}, Color{128, 255, 255, 128}}}));
    REQUIRE(q.size() == 1);
    near(q[0].pos[0], {-10, -20});  // (-20, 10) gegen den Uhrzeigersinn um 90°
    CHECK(q[0].color[0] == Color{128, 128, 0, 128});
}

TEST_CASE("banner_scene: Textur-Index außerhalb → kein Quad; ohne Textur-Map → Farbquad") {
    PaneSpec p = test::pane("pic1", "pic", 0, 0, 4, 4);
    CHECK(quads(parse({p}, {MatSpec{"m", {5}, {}, {}}})).empty());
    const auto q = quads(parse({p}, {MatSpec{"m", {}, {}, {}}}));
    REQUIRE(q.size() == 1);
    CHECK(q[0].texIndex == -1);
    p.material = 9;  // Material außerhalb
    CHECK(quads(parse({p})).empty());
}

TEST_CASE("banner_scene: unsichtbares Eltern-Pane blendet die Kinder aus") {
    PaneSpec root = test::pane("pan1", "root", 0, 0, 10, 10);
    root.flags = 0x02;
    root.children.push_back(test::pane("pic1", "pic", 0, 0, 4, 4));
    CHECK(quads(parse({root})).empty());
}

TEST_CASE("banner_scene: Textur-SRT verschiebt und skaliert die UV um die Mitte") {
    PaneSpec p = test::pane("pic1", "pic", 0, 0, 4, 4);
    const auto q = quads(parse({p}, {MatSpec{"m", {0}, {{0.25f, 0, 0, 2, 1}}, {}}}));
    REQUIRE(q.size() == 1);
    near(q[0].uv[0], {-0.25f, 0});  // (0 - 0.5) * 2 + 0.5 + 0.25
    near(q[0].uv[3], {1.75f, 1});
}

TEST_CASE("banner_scene: falsche Zustandsanzahl → leer") {
    const Layout l = parse({test::pane("pic1", "pic", 0, 0, 4, 4)});
    CHECK(buildQuads(l, {}).empty());
}
