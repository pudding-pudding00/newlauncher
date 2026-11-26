#include "ui/choice_dialog.hpp"
#include "support/fake_font.hpp"
#include <doctest/doctest.h>
#include <array>
using namespace nl;
using namespace nl::ui;

namespace {
const gfx::AtlasShapes kShapes{gfx::UvRect{0, 0, 0.01f, 0.01f}, gfx::UvRect{0.5f, 0.5f, 0.6f, 0.6f}};
constexpr std::array<const char*, 2> kTwo{"Wii", "Cloud"};
constexpr std::array<const char*, 3> kThree{"Wii behalten", "Cloud nehmen", "Ohne Abgleich"};

struct Fixture {
    gfx::FontSet fonts = test::fakeFontSet(8, 16);
    UiContext ctx;
    Fixture() {
        ctx.theme = &lightTheme();
        ctx.fonts = &fonts;
        ctx.shapes = &kShapes;
        ctx.width = 640;
        ctx.height = 480;
    }
    static InputFrame action(Action a) {
        InputFrame f;
        f.actions = 1u << static_cast<u32>(a);
        return f;
    }
    static InputFrame clickAt(gfx::Rect r) {
        InputFrame f;
        f.pointerValid = true;
        f.pointerMoved = true;
        f.pointerX = r.x + r.w / 2;
        f.pointerY = r.y + r.h / 2;
        f.actions = 1u << static_cast<u32>(Action::Confirm);
        return f;
    }
    SceneRequests step(ChoiceDialog& s, const InputFrame& in) {
        SceneRequests r;
        s.update(ctx, in, 16, r);
        return r;
    }
};
}  // namespace

TEST_CASE_FIXTURE(Fixture, "ChoiceDialog: Rechts und A wählt Knopf 1 und sendet das Kommando") {
    ChoiceDialog d;
    d.open("Konflikt", "Beide Stände sind neuer.", kTwo, AppCommand::ClearCovers);
    CHECK(d.isOverlay());
    CHECK(d.focused() == 0);
    step(d, action(Action::Right));
    CHECK(d.focused() == 1);
    const SceneRequests r = step(d, action(Action::Confirm));
    CHECK(r.pop);
    CHECK(r.command == AppCommand::ClearCovers);
    CHECK(d.picked() == 1);
}

TEST_CASE_FIXTURE(Fixture, "ChoiceDialog: B bricht ab mit -1 und sendet das Kommando") {
    ChoiceDialog d;
    d.open("T", "x", kTwo, AppCommand::ClearCovers, 1);
    const SceneRequests r = step(d, action(Action::Back));
    CHECK(r.pop);
    CHECK(r.command == AppCommand::ClearCovers);
    CHECK(d.picked() == -1);
}

TEST_CASE_FIXTURE(Fixture, "ChoiceDialog: Klick auf den dritten Knopf wählt 2") {
    ChoiceDialog d;
    d.open("T", "x", kThree, AppCommand::ClearCovers);
    const SceneRequests r = step(d, clickAt(d.buttonRect(ctx, 2)));
    CHECK(r.pop);
    CHECK(d.picked() == 2);
}

TEST_CASE_FIXTURE(Fixture, "ChoiceDialog: Rechts am Ende und Links am Anfang bleiben stehen") {
    ChoiceDialog d;
    d.open("T", "x", kTwo, AppCommand::ClearCovers);
    step(d, action(Action::Left));
    CHECK(d.focused() == 0);
    step(d, action(Action::Right));
    step(d, action(Action::Right));
    CHECK(d.focused() == 1);
    CHECK_FALSE(step(d, action(Action::Right)).pop);
}

TEST_CASE_FIXTURE(Fixture, "ChoiceDialog: drei Knöpfe passen nebeneinander in 640 px") {
    ChoiceDialog d;
    d.open("T", "x", kThree, AppCommand::ClearCovers);
    for (int i = 0; i < 3; ++i) {
        const gfx::Rect r = d.buttonRect(ctx, i);
        CHECK(r.x >= 0);
        CHECK(r.x + r.w <= 640);
        CHECK(r.w > 0);
        if (i > 0) CHECK(d.buttonRect(ctx, i - 1).x + d.buttonRect(ctx, i - 1).w <= r.x);
    }
    gfx::DrawList dl{4096, 16};
    d.draw(ctx, dl);  // zeichnet ohne Absturz
}
