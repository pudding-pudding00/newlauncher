#include "ui/startup_scene.hpp"
#include "support/fake_font.hpp"
#include <doctest/doctest.h>
using namespace nl;
using namespace nl::ui;

namespace {
const gfx::AtlasShapes kShapes{gfx::UvRect{0, 0, 0.01f, 0.01f}, gfx::UvRect{0.5f, 0.5f, 0.6f, 0.6f}};

struct Fixture {
    gfx::FontSet fonts = test::fakeFontSet(8, 16);
    UiContext ctx;
    gfx::DrawList dl{2048, 8};
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
    void step(StartupScene& s, const InputFrame& in) {
        SceneRequests r;
        s.update(ctx, in, 16, r);
    }
};
constexpr u32 kConfirm = 1u << static_cast<u32>(Action::Confirm);
}  // namespace

TEST_CASE_FIXTURE(Fixture, "Dialog nur im Zustand UsbFailed bedienbar") {
    StartupScene s;
    s.showWaitingUsb(12);
    step(s, action(Action::Confirm));
    CHECK(s.takeChoice() == StartupScene::Choice::None);
    s.showScanning(5);
    step(s, action(Action::Confirm));
    CHECK(s.takeChoice() == StartupScene::Choice::None);
}

TEST_CASE_FIXTURE(Fixture, "Confirm liefert Retry bzw. UseSd genau einmal") {
    StartupScene s;
    s.showUsbFailed();
    step(s, action(Action::Confirm));
    CHECK(s.takeChoice() == StartupScene::Choice::Retry);
    CHECK(s.takeChoice() == StartupScene::Choice::None);
    step(s, action(Action::Right));
    step(s, action(Action::Right));  // klemmt am Ende
    step(s, action(Action::Confirm));
    CHECK(s.takeChoice() == StartupScene::Choice::UseSd);
    step(s, action(Action::Left));
    step(s, action(Action::Confirm));
    CHECK(s.takeChoice() == StartupScene::Choice::Retry);
}

TEST_CASE_FIXTURE(Fixture, "Zeiger auf Knopf + Confirm; Zeiger daneben blockiert") {
    StartupScene s;
    s.showUsbFailed();
    const gfx::Rect b = s.buttonRect(ctx, 1);
    step(s, pointer(b.x + b.w / 2, b.y + b.h / 2, true));
    step(s, pointer(b.x + b.w / 2, b.y + b.h / 2, false, kConfirm));
    CHECK(s.takeChoice() == StartupScene::Choice::UseSd);
    step(s, pointer(5, 5, true));
    step(s, pointer(5, 5, false, kConfirm));
    CHECK(s.takeChoice() == StartupScene::Choice::None);
}

TEST_CASE_FIXTURE(Fixture, "showUsbFailed setzt den Fokus auf „Erneut versuchen\" zurück") {
    StartupScene s;
    s.showUsbFailed();
    step(s, action(Action::Right));
    s.showWaitingUsb(3);
    s.showUsbFailed();
    step(s, action(Action::Confirm));
    CHECK(s.takeChoice() == StartupScene::Choice::Retry);
}

TEST_CASE_FIXTURE(Fixture, "draw in allen Zuständen und Breiten ohne verworfene Quads") {
    for (float w : {640.0f, 854.0f}) {
        ctx.width = w;
        StartupScene s;
        for (int state = 0; state < 3; ++state) {
            if (state == 0) s.showWaitingUsb(15);
            if (state == 1) s.showUsbFailed();
            if (state == 2) s.showScanning(1234);
            dl.clear();
            s.draw(ctx, dl);
            CHECK(dl.quadCount() > 0);
            CHECK(dl.droppedQuads() == 0);
            for (const gfx::Vertex& v : dl.vertices()) {
                CHECK(v.x >= -1);
                CHECK(v.x <= w + 1);
            }
        }
    }
}

TEST_CASE_FIXTURE(Fixture, "showUsbFailed jeden Frame (wie in main) setzt den Fokus nicht zurück") {
    StartupScene s;
    s.showUsbFailed();
    step(s, action(Action::Right));
    s.showUsbFailed();  // main ruft das im Zustand TimedOut in jedem Frame auf
    step(s, action(Action::Confirm));
    CHECK(s.takeChoice() == StartupScene::Choice::UseSd);
}
