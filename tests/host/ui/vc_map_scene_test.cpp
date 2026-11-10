#include "ui/vc_map_scene.hpp"
#include "support/fake_font.hpp"
#include <doctest/doctest.h>
#include <vector>
using namespace nl;
using namespace nl::ui;

namespace {
const gfx::AtlasShapes kShapes{gfx::UvRect{0, 0, 0.01f, 0.01f}, gfx::UvRect{0.5f, 0.5f, 0.6f, 0.6f}};

constexpr u64 kSm64Vc = 0x000100014E414D50ULL;
constexpr u64 kZeldaVc = 0x000100014E414B50ULL;
constexpr u64 kForeign = 0x0001000146464646ULL;

struct Fixture {
    gfx::FontSet fonts = test::fakeFontSet(8, 16);
    UiContext ctx;
    gfx::DrawList dl{8192, 16};
    Fixture() {
        ctx.theme = &lightTheme();
        ctx.fonts = &fonts;
        ctx.shapes = &kShapes;
    }
    static InputFrame action(Action a) {
        InputFrame f;
        f.actions = 1u << static_cast<u32>(a);
        return f;
    }
    SceneRequests step(VcMapScene& s, Action a) {
        SceneRequests r;
        s.update(ctx, action(a), 16, r);
        return r;
    }
};

std::vector<ChannelInfo> twoChannels() { return {{kSm64Vc, "Super Mario 64"}, {kZeldaVc, "Zelda"}}; }

std::vector<VcMapRow> rows() {
    return {
        {"n64/sm64.z64", "Super Mario 64", std::nullopt, std::size_t{0}},
        {"n64/zelda.z64", "Zelda", kZeldaVc, std::size_t{1}},
        {"n64/fzero.z64", "F-Zero X", std::nullopt, std::nullopt},
    };
}
}  // namespace

TEST_CASE_FIXTURE(Fixture, "open übernimmt Zuordnungen, Fokus auf Zeile 0, nicht dirty") {
    VcMapScene s;
    s.open(rows(), twoChannels());
    CHECK(s.focusedRow() == 0);
    CHECK_FALSE(s.dirty());
    CHECK(s.rows().size() == 3);
    CHECK(s.channelCount() == 2);
    CHECK_FALSE(s.selection(0).has_value());
    CHECK(s.selection(1) == kZeldaVc);
    CHECK_FALSE(s.selection(2).has_value());
}

TEST_CASE_FIXTURE(Fixture, "Runter/Hoch bewegt den Fokus mit Begrenzung") {
    VcMapScene s;
    s.open(rows(), twoChannels());
    step(s, Action::Up);
    CHECK(s.focusedRow() == 0);
    for (int i = 0; i < 5; ++i) step(s, Action::Down);
    CHECK(s.focusedRow() == 2);
    step(s, Action::Up);
    CHECK(s.focusedRow() == 1);
}

TEST_CASE_FIXTURE(Fixture, "Rechts durchläuft — → Kanal 0 → … → —, setzt dirty") {
    VcMapScene s;
    s.open(rows(), twoChannels());
    step(s, Action::Right);
    CHECK(s.selection(0) == kSm64Vc);
    CHECK(s.dirty());
    step(s, Action::Right);
    CHECK(s.selection(0) == kZeldaVc);
    step(s, Action::Right);
    CHECK_FALSE(s.selection(0).has_value());
    step(s, Action::Left);
    CHECK(s.selection(0) == kZeldaVc);
}

TEST_CASE_FIXTURE(Fixture, "A übernimmt den Vorschlag nur ohne Auswahl") {
    VcMapScene s;
    s.open(rows(), twoChannels());
    step(s, Action::Confirm);
    CHECK(s.selection(0) == kSm64Vc);
    CHECK(s.dirty());

    VcMapScene t;
    t.open(rows(), twoChannels());
    step(t, Action::Down);  // Zelda: zugeordnet, Vorschlag ändert nichts
    step(t, Action::Confirm);
    CHECK(t.selection(1) == kZeldaVc);
    CHECK_FALSE(t.dirty());
    step(t, Action::Down);  // F-Zero: kein Vorschlag
    step(t, Action::Confirm);
    CHECK_FALSE(t.selection(2).has_value());
    CHECK_FALSE(t.dirty());
}

TEST_CASE_FIXTURE(Fixture, "Zuordnung auf nicht installierten Kanal bleibt erhalten, bis sie geändert wird") {
    std::vector<VcMapRow> r = rows();
    r[0].mapped = kForeign;
    VcMapScene s;
    s.open(r, twoChannels());
    CHECK(s.selection(0) == kForeign);
    step(s, Action::Down);
    step(s, Action::Up);
    CHECK(s.selection(0) == kForeign);
    CHECK_FALSE(s.dirty());
    step(s, Action::Right);
    CHECK(s.selection(0) == kSm64Vc);
}

TEST_CASE_FIXTURE(Fixture, "B schließt; SaveVcMap nur wenn dirty") {
    VcMapScene s;
    s.open(rows(), twoChannels());
    SceneRequests r = step(s, Action::Back);
    CHECK(r.pop);
    CHECK(r.command == AppCommand::None);

    s.open(rows(), twoChannels());
    step(s, Action::Right);
    r = step(s, Action::Back);
    CHECK(r.pop);
    CHECK(r.command == AppCommand::SaveVcMap);
}

TEST_CASE_FIXTURE(Fixture, "ohne Kanäle: Rechts/A wirkungslos, B schließt ohne Speichern") {
    VcMapScene s;
    s.open(rows(), {});
    step(s, Action::Right);
    step(s, Action::Left);
    step(s, Action::Confirm);
    CHECK_FALSE(s.selection(0).has_value());
    CHECK_FALSE(s.dirty());
    const SceneRequests r = step(s, Action::Back);
    CHECK(r.pop);
    CHECK(r.command == AppCommand::None);
}

TEST_CASE_FIXTURE(Fixture, "draw mit vielen Zeilen, ohne Zeilen und ohne Kanäle bleibt im Bild") {
    std::vector<VcMapRow> many;
    for (int i = 0; i < 20; ++i) many.push_back({"n64/x.z64", "Ein sehr langer Spieltitel, der gekürzt werden muss", kForeign, std::nullopt});
    for (float w : {640.0f, 854.0f}) {
        ctx.width = w;
        for (int variant = 0; variant < 3; ++variant) {
            VcMapScene s;
            if (variant == 0) s.open(many, twoChannels());
            if (variant == 1) s.open({}, twoChannels());
            if (variant == 2) s.open(rows(), {});
            for (int i = 0; i < 15; ++i) step(s, Action::Down);
            dl.clear();
            s.draw(ctx, dl);
            CHECK(dl.droppedQuads() == 0);
            for (const gfx::Vertex& v : dl.vertices()) {
                CHECK(v.x >= -4);
                CHECK(v.x <= w + 4);
                CHECK(v.y >= -4);
                CHECK(v.y <= 484);
            }
        }
    }
}
