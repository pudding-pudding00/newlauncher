#include "ui/home_menu.hpp"
#include "ui/main_scene.hpp"
#include "ui/test_library.hpp"
#include "support/fake_font.hpp"
#include <doctest/doctest.h>
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
    SceneRequests step(HomeMenu& m, const InputFrame& in) {
        SceneRequests r;
        m.update(ctx, in, 16, r);
        return r;
    }
};
constexpr u32 kConfirm = 1u << static_cast<u32>(Action::Confirm);
}  // namespace

TEST_CASE_FIXTURE(Fixture, "HomeMenu ist ein Overlay; onEnter setzt Fokus 0") {
    HomeMenu m;
    CHECK(m.isOverlay());
    m.onEnter();
    step(m, action(Action::Right));
    CHECK(m.focus() == 1);
    m.onEnter();
    CHECK(m.focus() == 0);
}

TEST_CASE_FIXTURE(Fixture, "Right bis zum Ende, Left bis zum Anfang, kein Umlauf") {
    HomeMenu m;
    m.onEnter();
    for (int i = 0; i < 6; ++i) step(m, action(Action::Right));
    CHECK(m.focus() == 3);
    for (int i = 0; i < 6; ++i) step(m, action(Action::Left));
    CHECK(m.focus() == 0);
}

TEST_CASE_FIXTURE(Fixture, "Confirm auf den Kacheln") {
    const AppCommand expected[4] = {AppCommand::ExitToHbc, AppCommand::WiiMenu, AppCommand::None,
                                    AppCommand::PowerOff};
    for (int i = 0; i < 4; ++i) {
        HomeMenu m;
        m.onEnter();
        for (int k = 0; k < i; ++k) step(m, action(Action::Right));
        const SceneRequests r = step(m, action(Action::Confirm));
        CHECK(r.command == expected[i]);
        CHECK_FALSE(r.pop);
    }
}

TEST_CASE_FIXTURE(Fixture, "Home schließt; Back schließt") {
    HomeMenu m;
    m.onEnter();
    CHECK(step(m, action(Action::Home)).pop);
    CHECK(step(m, action(Action::Back)).pop);
}

TEST_CASE_FIXTURE(Fixture, "Zeiger bewegt auf Kachel 3 → Fokus 3; ruhender Zeiger ändert den Fokus nicht") {
    HomeMenu m;
    m.onEnter();
    const gfx::Rect r = m.tileRect(ctx, 3);
    step(m, pointer(r.x + r.w / 2, r.y + r.h / 2, true));
    CHECK(m.focus() == 3);
    step(m, action(Action::Left));
    step(m, pointer(r.x + r.w / 2, r.y + r.h / 2, false));
    CHECK(m.focus() == 2);
}

TEST_CASE_FIXTURE(Fixture, "Zeiger außerhalb der Kacheln + Confirm: kein Kommando") {
    HomeMenu m;
    m.onEnter();
    step(m, pointer(5, 5, true));
    const SceneRequests r = step(m, pointer(5, 5, false, kConfirm));
    CHECK(r.command == AppCommand::None);
    CHECK_FALSE(r.pop);
}

TEST_CASE_FIXTURE(Fixture, "ruhender Zeiger neben den Kacheln direkt nach dem Öffnen blockiert Confirm") {
    HomeMenu m;
    m.onEnter();
    const SceneRequests r = step(m, pointer(5, 470, false, kConfirm));
    CHECK(r.command == AppCommand::None);
}

TEST_CASE_FIXTURE(Fixture, "Richtungstaste übernimmt trotz ruhendem Zeiger neben den Kacheln") {
    HomeMenu m;
    m.onEnter();
    step(m, pointer(5, 470, false, 1u << static_cast<u32>(Action::Right)));
    CHECK(step(m, pointer(5, 470, false, kConfirm)).command == AppCommand::WiiMenu);
}

TEST_CASE_FIXTURE(Fixture, "Zeiger auf Kachel + Confirm löst diese Kachel aus") {
    HomeMenu m;
    m.onEnter();
    const gfx::Rect r = m.tileRect(ctx, 1);
    step(m, pointer(r.x + 5, r.y + 5, true));
    CHECK(step(m, pointer(r.x + 5, r.y + 5, false, kConfirm)).command == AppCommand::WiiMenu);
}

TEST_CASE_FIXTURE(Fixture, "im Stapel: Home öffnet das Menü über MainScene, Home schließt es wieder") {
    const Library lib = testLibrary();
    MainScene main(lib);
    HomeMenu home;
    main.setHomeMenu(&home);
    SceneStack st;
    st.push(&main);
    st.update(ctx, action(Action::Home), 16);
    CHECK(st.depth() == 2);
    st.update(ctx, InputFrame{}, 200);
    st.update(ctx, action(Action::Home), 16);
    st.update(ctx, InputFrame{}, 100);
    CHECK(st.depth() == 2);
    st.update(ctx, InputFrame{}, 100);
    CHECK(st.depth() == 1);
}

TEST_CASE_FIXTURE(Fixture, "draw in 640 und 854: keine verworfenen Quads, alles im Bild") {
    for (float w : {640.0f, 854.0f}) {
        ctx.width = w;
        HomeMenu m;
        m.onEnter();
        dl.clear();
        m.draw(ctx, dl);
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
