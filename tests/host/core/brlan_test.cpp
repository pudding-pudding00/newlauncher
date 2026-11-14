#include "core/brlan.hpp"
#include "support/brlan_images.hpp"

#include <doctest/doctest.h>

using namespace nl;
using test::EntrySpec;
using test::InfoSpec;
using test::TagSpec;

namespace {
Track hermite(std::vector<Key> keys) {
    Track t;
    t.keys = std::move(keys);
    return t;
}

Anim parse(const std::vector<u8>& file) {
    auto r = parseBrlan(file);
    REQUIRE(r.isOk());
    return r.value();
}

Layout layout() {
    test::PaneSpec root = test::pane("pan1", "root", 0, 0, 608, 456);
    root.children.push_back(test::pane("pic1", "logo", 5, 0, 40, 20));
    auto r = parseBrlyt(test::buildBrlyt(608, 456, {"a.tpl", "b.tpl"}, {test::MatSpec{"m", {0}, {}, {}}}, {root}));
    REQUIRE(r.isOk());
    return r.value();
}
}  // namespace

TEST_CASE("brlan: Hermite mit Steigung 0 ist symmetrisch, ein Key ist konstant") {
    CHECK(evalTrack(hermite({{0, 0, 0}, {10, 100, 0}}), 5) == doctest::Approx(50));
    CHECK(evalTrack(hermite({{0, 0, 0}, {10, 100, 0}}), 2.5f) == doctest::Approx(15.625f));
    CHECK(evalTrack(hermite({{3, 7, 1}}), 0) == doctest::Approx(7));
    CHECK(evalTrack(hermite({{3, 7, 1}}), 99) == doctest::Approx(7));
    CHECK(evalTrack(hermite({}), 1) == 0.0f);
}

TEST_CASE("brlan: vor dem ersten und nach dem letzten Key gilt dessen Wert") {
    const Track t = hermite({{10, 4, 0}, {20, 8, 0}});
    CHECK(evalTrack(t, 0) == doctest::Approx(4));
    CHECK(evalTrack(t, 500) == doctest::Approx(8));
}

TEST_CASE("brlan: Hermite mit Steigung") {
    // Lineare Steigung 10 je Bild auf beiden Seiten ergibt eine Gerade.
    CHECK(evalTrack(hermite({{0, 0, 10}, {10, 100, 10}}), 3) == doctest::Approx(30));
}

TEST_CASE("brlan: Datei mit RLPA, unsortierte Keys werden sortiert") {
    const auto file = test::buildBrlan(
        60, true, {}, {EntrySpec{"logo", false, {TagSpec{"RLPA", {InfoSpec{0, 0, false, {{60, 120, 0}, {0, 0, 0}}}}}}}});
    const Anim a = parse(file);
    CHECK(a.frames == 60);
    CHECK(a.loop);
    REQUIRE(a.tracks.size() == 1);
    CHECK(a.tracks[0].pane == "logo");
    CHECK_FALSE(a.tracks[0].material);
    CHECK(a.tracks[0].target == AnimTarget::TransX);
    REQUIRE(a.tracks[0].keys.size() == 2);
    CHECK(a.tracks[0].keys[0].frame == 0);
    CHECK(a.tracks[0].keys[1].frame == 60);
}

TEST_CASE("brlan: applyAnim setzt trans.x des Panes bei Bild 30") {
    const auto file = test::buildBrlan(
        60, true, {}, {EntrySpec{"logo", false, {TagSpec{"RLPA", {InfoSpec{0, 0, false, {{0, 0, 0}, {60, 120, 0}}}}}}}});
    const Anim a = parse(file);
    const Layout l = layout();
    auto state = initialState(l);
    applyAnim(l, a, 30, state);
    CHECK(state[1].trans.x == doctest::Approx(60));
    CHECK(state[0].trans.x == doctest::Approx(0));  // anderes Pane unverändert
}

TEST_CASE("brlan: RLTP wechselt texIndex ohne Zwischenwerte, über die Musterliste") {
    // Muster: b.tpl (Layout-Index 1), a.tpl (0). Bild 0–9 → b, ab 10 → a.
    const auto file = test::buildBrlan(20, true, {"b.tpl", "a.tpl"},
                                       {EntrySpec{"m", true, {TagSpec{"RLTP", {InfoSpec{0, 0, true, {{0, 0, 0}, {10, 1, 0}}}}}}}});
    const Anim a = parse(file);
    REQUIRE(a.tracks.size() == 1);
    CHECK(a.tracks[0].step);
    CHECK(a.tracks[0].target == AnimTarget::TexPattern);
    CHECK(a.patternTextures == std::vector<std::string>{"b.tpl", "a.tpl"});
    CHECK(evalTrack(a.tracks[0], 9.9f) == 0.0f);
    const Layout l = layout();
    auto state = initialState(l);
    applyAnim(l, a, 9, state);
    CHECK(state[1].texIndex == 1);
    applyAnim(l, a, 10, state);
    CHECK(state[1].texIndex == 0);
}

TEST_CASE("brlan: RLVC-Alpha, RLMC, RLTS und RLVI") {
    const auto file = test::buildBrlan(
        10, false, {},
        {EntrySpec{"logo", false,
                   {TagSpec{"RLVC", {InfoSpec{0, 0x10, false, {{0, 100, 0}}}, InfoSpec{0, 0x05, false, {{0, 300, 0}}}}},
                    TagSpec{"RLVI", {InfoSpec{0, 0, true, {{0, 0, 0}}}}}}},
         EntrySpec{"m", true,
                   {TagSpec{"RLMC", {InfoSpec{0, 3, false, {{0, 64, 0}}}}},
                    TagSpec{"RLTS", {InfoSpec{0, 2, false, {{0, 45, 0}}}, InfoSpec{1, 0, false, {{0, 9, 0}}}}}}}});
    const Anim a = parse(file);
    const Layout l = layout();
    auto state = initialState(l);
    applyAnim(l, a, 0, state);
    CHECK(state[1].alpha == 100);
    CHECK(state[1].vtxColor[1].g == 255);  // 300 wird auf 255 begrenzt
    CHECK_FALSE(state[1].visible);
    CHECK(state[1].matColor.a == 64);
    CHECK(state[1].texSrt[2] == doctest::Approx(45));
    CHECK(state[1].texSrt[0] == doctest::Approx(0));  // SRT 1 wird nicht gezeichnet
}

TEST_CASE("brlan: unbekannte Spurtypen werden übersprungen, kaputte Dateien abgelehnt") {
    const auto file = test::buildBrlan(5, true, {}, {EntrySpec{"logo", false, {TagSpec{"RLIM", {InfoSpec{0, 0, false, {{0, 1, 0}}}}}}}});
    const Anim a = parse(file);
    CHECK(a.tracks.empty());
    CHECK(a.skipped == 1);
    auto cut = file;
    cut.resize(cut.size() - 8);
    CHECK_FALSE(parseBrlan(cut).isOk());
    auto magic = file;
    magic[0] = 'X';
    CHECK_FALSE(parseBrlan(magic).isOk());
    auto keys = file;  // Keyanzahl weit hinter dem Ende
    keys[keys.size() - 12 - 8] = 0xFF;
    CHECK_FALSE(parseBrlan(keys).isOk());
}

TEST_CASE("brlan: AnimPlayer spielt Start einmal, dann Loop in Schleife") {
    Anim start, loop;
    start.frames = 3;
    loop.frames = 2;
    AnimPlayer p;
    p.reset(&start, &loop);
    CHECK(p.current() == &start);
    p.step();
    p.step();
    CHECK(p.frame() == 2);
    p.step();
    CHECK(p.current() == &loop);
    CHECK(p.frame() == 0);
    p.step();
    p.step();
    CHECK(p.current() == &loop);
    CHECK(p.frame() == 0);
    p.reset(nullptr, &loop);
    CHECK(p.current() == &loop);
    Anim empty;
    p.reset(nullptr, &empty);
    p.step();
    CHECK(p.frame() == 0);  // 0 Bilder: bleibt stehen
    p.reset(nullptr, nullptr);
    CHECK(p.current() == nullptr);
}

TEST_CASE("brlan: AnimPlayer::advance läuft nach Zeit, auch in Bruchteilen") {
    Anim start, loop;
    start.frames = 3;
    loop.frames = 2;
    AnimPlayer p;
    p.reset(&start, &loop);
    p.advance(1.2f);  // ein PAL-Bild (50 Hz) = 1,2 Animationsbilder
    CHECK(p.frame() == doctest::Approx(1.2f));
    p.advance(1.2f);
    CHECK(p.current() == &start);
    CHECK(p.frame() == doctest::Approx(2.4f));
    p.advance(1.2f);  // 3,6 → Start zu Ende, 0,6 in der Schleife
    CHECK(p.current() == &loop);
    CHECK(p.frame() == doctest::Approx(0.6f));
    p.advance(1.6f);  // 2,2 → Schleife umgelaufen
    CHECK(p.current() == &loop);
    CHECK(p.frame() == doctest::Approx(0.2f));
    p.advance(1001.0f);  // großer Sprung: bleibt im Bereich
    CHECK(p.frame() >= 0.0f);
    CHECK(p.frame() < 2.0f);
    p.reset(&start, nullptr);
    p.advance(10.0f);  // ohne Schleife: letztes Bild halten
    CHECK(p.current() == &start);
    CHECK(p.frame() == doctest::Approx(2.0f));
    p.advance(-5.0f);  // rückwärts gibt es nicht
    CHECK(p.frame() == doctest::Approx(2.0f));
}

TEST_CASE("brlan: Grenzen gegen Speicher-Aufblähen") {
    SUBCASE("Mustername über 255 Zeichen") {
        CHECK_FALSE(parseBrlan(test::buildBrlan(10, true, {std::string(300, 'p')}, {})).isOk());
    }
    SUBCASE("mehr Mustertexturen als Layout-Texturen") {
        std::vector<std::string> many(kLytMaxTextures + 1, "x.tpl");
        CHECK_FALSE(parseBrlan(test::buildBrlan(10, true, many, {})).isOk());
    }
    SUBCASE("mehr Keys insgesamt als kAnimMaxTotalKeys") {
        InfoSpec info;
        for (u32 k = 0; k < kAnimMaxKeys; ++k) info.keys.push_back({static_cast<float>(k), 0, 0});
        TagSpec tag{"RLPA", {}};
        for (u32 i = 0; i <= kAnimMaxTotalKeys / kAnimMaxKeys; ++i) tag.infos.push_back(info);
        CHECK_FALSE(parseBrlan(test::buildBrlan(10, true, {}, {EntrySpec{"logo", false, {tag}}})).isOk());
    }
}

TEST_CASE("brlan: animWork zählt die Arbeit je Bild wie applyAnim") {
    const Layout l = layout();  // 2 Panes, 1 Material „m“
    Anim a;
    Track pane;
    pane.pane = "logo";
    pane.keys = {Key{0, 1, 0}};
    Track mat;
    mat.pane = "m";
    mat.material = true;
    mat.keys = {Key{0, 1, 0}};
    Track empty;
    empty.pane = "logo";
    a.tracks = {pane, mat, empty};
    CHECK(animWork(l, a) == 2 + (1 + 1 * 2) + 0);
}
