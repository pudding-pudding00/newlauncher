#include "core/brlyt.hpp"
#include "support/brlyt_images.hpp"

#include <doctest/doctest.h>

using namespace nl;
using test::MatSpec;
using test::PaneSpec;

namespace {
std::vector<u8> sample() {
    PaneSpec root = test::pane("pan1", "RootPane", 0, 0, 608, 456);
    PaneSpec pic = test::pane("pic1", "logo", 10, 20, 40, 20);
    pic.material = 1;
    pic.vtx[1] = Color{1, 2, 3, 4};
    pic.uvs.push_back({0, 0, 2, 0, 0, 1, 2, 1});
    root.children.push_back(pic);
    root.children.push_back(test::pane("txt1", "title", 0, 0, 10, 10));
    return test::buildBrlyt(608, 456, {"a.tpl", "logo.tpl"},
                            {MatSpec{"bg", {0}, {}, {}}, MatSpec{"logo", {1}, {{0.5f, 0, 90, 2, 1}}, Color{10, 20, 30, 40}}},
                            {root});
}
}  // namespace

TEST_CASE("brlyt: Größe, Texturen, Materialien und Panes") {
    auto r = parseBrlyt(sample());
    REQUIRE(r.isOk());
    const Layout& l = r.value();
    CHECK(l.width == 608.0f);
    CHECK(l.height == 456.0f);
    REQUIRE(l.textures.size() == 2);
    CHECK(l.textures[1] == "logo.tpl");
    REQUIRE(l.materials.size() == 2);
    CHECK(l.materials[1].name == "logo");
    CHECK(l.materials[1].texIndex == 1);
    CHECK(l.materials[1].texSrt[2] == 90.0f);
    CHECK(l.materials[1].color == Color{10, 20, 30, 40});
    CHECK(l.materials[0].color == Color{255, 255, 255, 255});  // ohne Block weiß
    REQUIRE(l.panes.size() == 3);
    CHECK(l.panes[0].name == "RootPane");
    CHECK(l.panes[0].parent == -1);
    CHECK_FALSE(l.panes[0].isPicture);
    const LytPane& pic = l.panes[1];
    CHECK(pic.name == "logo");
    CHECK(pic.parent == 0);
    CHECK(pic.isPicture);
    CHECK(pic.material == 1);
    CHECK(pic.trans.x == 10.0f);
    CHECK(pic.size.y == 20.0f);
    CHECK(pic.vtxColor[1] == Color{1, 2, 3, 4});
    CHECK(pic.uv[1] == Vec2{2, 0});
    CHECK(l.panes[2].parent == 0);  // txt1 bleibt als Gruppe im Baum
    CHECK_FALSE(l.panes[2].isPicture);
}

TEST_CASE("brlyt: txt1 wird übersprungen und gezählt") {
    auto r = parseBrlyt(sample());
    REQUIRE(r.isOk());
    CHECK(r.value().skipped == 1);
}

TEST_CASE("brlyt: unbekannter Abschnitt wird übersprungen, Kinder bleiben am richtigen Eltern-Pane") {
    PaneSpec root = test::pane("pan1", "root", 0, 0, 10, 10);
    PaneSpec bnd = test::pane("bnd1", "bound", 0, 0, 10, 10);
    bnd.children.push_back(test::pane("pan1", "inner", 0, 0, 1, 1));
    root.children.push_back(bnd);
    root.children.push_back(test::pane("pan1", "after", 0, 0, 1, 1));
    auto r = parseBrlyt(test::buildBrlyt(10, 10, {}, {}, {root}));
    REQUIRE(r.isOk());
    REQUIRE(r.value().panes.size() == 4);
    CHECK(r.value().panes[2].parent == 1);  // inner unter bound
    CHECK(r.value().panes[3].parent == 0);  // after wieder unter root
    CHECK(r.value().skipped == 1);
}

TEST_CASE("brlyt: kaputte Dateien → InvalidData") {
    auto bad = sample();
    bad[0] = 'X';
    CHECK(parseBrlyt(bad).error().code == ErrorCode::InvalidData);
    auto cut = sample();
    cut.resize(cut.size() - 8);
    CHECK(parseBrlyt(cut).error().code == ErrorCode::InvalidData);
    auto tiny = sample();
    test::putBe32(tiny, 0x10 + 4, 4);  // lyt1 mit Länge < 8
    CHECK(parseBrlyt(tiny).error().code == ErrorCode::InvalidData);
    CHECK(parseBrlyt(std::vector<u8>(4, 0)).error().code == ErrorCode::InvalidData);
    // pae1 ohne pas1
    PaneSpec root = test::pane("pan1", "r", 0, 0, 1, 1);
    auto extra = test::buildBrlyt(1, 1, {}, {}, {root});
    const std::vector<u8> pae{'p', 'a', 'e', '1', 0, 0, 0, 8};
    extra.insert(extra.end(), pae.begin(), pae.end());
    test::putBe32(extra, 8, static_cast<u32>(extra.size()));
    extra[0x0F] = static_cast<u8>(extra[0x0F] + 1);
    CHECK(parseBrlyt(extra).error().code == ErrorCode::InvalidData);
}

TEST_CASE("brlyt: Bild mit Materialindex außerhalb behält material = -1") {
    PaneSpec pic = test::pane("pic1", "p", 0, 0, 1, 1);
    pic.material = 7;
    auto r = parseBrlyt(test::buildBrlyt(1, 1, {"t.tpl"}, {MatSpec{"m", {0}, {}, {}}}, {pic}));
    REQUIRE(r.isOk());
    CHECK(r.value().panes[0].material == -1);
}

TEST_CASE("brlyt: Texturname über 255 Zeichen wird abgelehnt") {
    test::PaneSpec root = test::pane("pan1", "root", 0, 0, 608, 456);
    CHECK_FALSE(parseBrlyt(test::buildBrlyt(608, 456, {std::string(300, 't')}, {}, {root})).isOk());
    CHECK(parseBrlyt(test::buildBrlyt(608, 456, {std::string(255, 't')}, {}, {root})).isOk());
}
