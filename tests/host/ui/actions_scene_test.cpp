#include "ui/actions_scene.hpp"
#include "support/fake_font.hpp"
#include <doctest/doctest.h>
#include <string>
using namespace nl;
using namespace nl::ui;

namespace {
const gfx::AtlasShapes kShapes{gfx::UvRect{0, 0, 0.01f, 0.01f}, gfx::UvRect{0.5f, 0.5f, 0.6f, 0.6f}};

struct Fixture {
    gfx::FontSet fonts = test::fakeFontSet(8, 16);
    UiContext ctx;
    gfx::DrawList dl{4096, 16};
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
    static InputFrame pointer(float x, float y, bool moved, u32 actions = 0) {
        InputFrame f;
        f.pointerValid = true;
        f.pointerMoved = moved;
        f.pointerX = x;
        f.pointerY = y;
        f.actions = actions;
        return f;
    }
    SceneRequests step(ActionsScene& s, const InputFrame& in) {
        SceneRequests r;
        s.update(ctx, in, 16, r);
        return r;
    }
};
constexpr u32 kConfirm = 1u << static_cast<u32>(Action::Confirm);
}  // namespace

TEST_CASE_FIXTURE(Fixture, "zwei Einträge, Overlay, onEnter setzt Fokus 0") {
    ActionsScene s;
    CHECK(ActionsScene::kCount == 2);
    CHECK(s.isOverlay());
    s.onEnter();
    step(s, action(Action::Down));
    CHECK(s.focus() == 1);
    step(s, action(Action::Down));
    CHECK(s.focus() == 1);  // kein Umlauf
    s.onEnter();
    CHECK(s.focus() == 0);
}

TEST_CASE_FIXTURE(Fixture, "A auf Eintrag 0: OpenVcMap und pop") {
    ActionsScene s;
    s.onEnter();
    const SceneRequests r = step(s, action(Action::Confirm));
    CHECK(r.command == AppCommand::OpenVcMap);
    CHECK(r.pop);
}

TEST_CASE_FIXTURE(Fixture, "A auf Eintrag 1: FetchCovers und pop") {
    ActionsScene s;
    s.onEnter();
    step(s, action(Action::Down));
    const SceneRequests r = step(s, action(Action::Confirm));
    CHECK(r.command == AppCommand::FetchCovers);
    CHECK(r.pop);
    CHECK(std::string(appCommandName(AppCommand::FetchCovers)) == "fetch_covers");
}

TEST_CASE_FIXTURE(Fixture, "B schließt nur") {
    ActionsScene s;
    s.onEnter();
    step(s, action(Action::Down));
    const SceneRequests r = step(s, action(Action::Back));
    CHECK(r.pop);
    CHECK(r.command == AppCommand::None);
}

TEST_CASE_FIXTURE(Fixture, "Zeiger auf Eintrag 1 + A löst ihn aus; Zeiger daneben blockiert A") {
    ActionsScene s;
    s.onEnter();
    const gfx::Rect r = s.rowRect(ctx, 1);
    step(s, pointer(r.x + 5, r.y + 5, true));
    CHECK(s.focus() == 1);
    CHECK(step(s, pointer(r.x + 5, r.y + 5, false, kConfirm)).command == AppCommand::FetchCovers);

    ActionsScene t;
    t.onEnter();
    const SceneRequests off = step(t, pointer(2, 2, true, kConfirm));
    CHECK(off.command == AppCommand::None);
    CHECK_FALSE(off.pop);
}

TEST_CASE_FIXTURE(Fixture, "draw in 640 und 854: keine verworfenen Quads, alles im Bild") {
    for (float w : {640.0f, 854.0f}) {
        ctx.width = w;
        ActionsScene s;
        s.onEnter();
        dl.clear();
        s.draw(ctx, dl);
        CHECK(dl.droppedQuads() == 0);
        CHECK(dl.quadCount() > 0);
        for (const gfx::Vertex& v : dl.vertices()) {
            CHECK(v.x >= -4);
            CHECK(v.x <= w + 4);
            CHECK(v.y >= -4);
            CHECK(v.y <= 484);
        }
    }
}
