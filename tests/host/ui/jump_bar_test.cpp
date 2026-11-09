#include "ui/jump_bar.hpp"
#include "ui/main_scene.hpp"
#include "ui/test_library.hpp"
#include "support/fake_font.hpp"
#include <doctest/doctest.h>
#include <string>
using namespace nl;
using namespace nl::ui;

namespace {
const gfx::AtlasShapes kShapes{gfx::UvRect{0, 0, 0.01f, 0.01f}, gfx::UvRect{0.5f, 0.5f, 0.6f, 0.6f}};

struct SpyTarget final : JumpTarget {
    char jumped = 0;
    int searches = 0;
    char current = 'M';
    void jumpToBucket(char b) override { jumped = b; }
    void openSearch() override { ++searches; }
    bool hasBucket(char b) const override { return b == 'M' || b == 'S'; }
    char currentBucket() const override { return current; }
};

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
    template <typename S>
    SceneRequests step(S& s, const InputFrame& in, float dt = 16) {
        SceneRequests r;
        s.update(ctx, in, dt, r);
        return r;
    }
};
constexpr u32 kConfirm = 1u << static_cast<u32>(Action::Confirm);
}  // namespace

TEST_CASE_FIXTURE(Fixture, "JumpBar: onEnter fokussiert den Buchstaben des aktuellen Spiels") {
    SpyTarget t;
    JumpBar b(t);
    b.onEnter();
    CHECK(b.focus() == 'M' - 'A' + 1);
    t.current = '#';
    b.onEnter();
    CHECK(b.focus() == 0);
}

TEST_CASE_FIXTURE(Fixture, "JumpBar: Left/Right ohne Umlauf, Confirm springt und schließt, Feld 27 öffnet Suche") {
    SpyTarget t;
    t.current = '#';
    JumpBar b(t);
    b.onEnter();
    step(b, action(Action::Left));
    CHECK(b.focus() == 0);
    step(b, action(Action::Right));
    SceneRequests r = step(b, action(Action::Confirm));
    CHECK(t.jumped == 'A');
    CHECK(r.pop);
    for (int i = 0; i < 40; ++i) step(b, action(Action::Right));
    CHECK(b.focus() == 27);
    r = step(b, action(Action::Confirm));
    CHECK(t.searches == 1);
    CHECK(r.pop);
}

TEST_CASE_FIXTURE(Fixture, "JumpBar: Back, Search und Home schließen") {
    SpyTarget t;
    JumpBar b(t);
    CHECK(step(b, action(Action::Back)).pop);
    CHECK(step(b, action(Action::Search)).pop);
    CHECK(step(b, action(Action::Home)).pop);
    CHECK(t.jumped == 0);
}

TEST_CASE_FIXTURE(Fixture, "JumpBar: Zeiger auf Feld + Confirm; Zeiger neben der Leiste blockiert Confirm") {
    SpyTarget t;
    JumpBar b(t);
    b.onEnter();
    const gfx::Rect s = b.cellRect(ctx, 'S' - 'A' + 1);
    step(b, pointer(s.x + s.w / 2, s.y + s.h / 2, true));
    step(b, pointer(s.x + s.w / 2, s.y + s.h / 2, false, kConfirm));
    CHECK(t.jumped == 'S');
    t.jumped = 0;
    b.onEnter();
    step(b, pointer(5, 5, true));
    CHECK_FALSE(step(b, pointer(5, 5, false, kConfirm)).pop);
    CHECK(t.jumped == 0);
}

TEST_CASE_FIXTURE(Fixture, "draw in 640 und 854: alle 28 Felder im Bild, keine verworfenen Quads") {
    for (float w : {640.0f, 854.0f}) {
        ctx.width = w;
        SpyTarget t;
        JumpBar b(t);
        b.onEnter();
        dl.clear();
        b.draw(ctx, dl);
        CHECK(dl.droppedQuads() == 0);
        for (int i = 0; i < 28; ++i) {
            const gfx::Rect r = b.cellRect(ctx, i);
            CHECK(r.x >= 0);
            CHECK(r.x + r.w <= w);
            CHECK(r.w >= 10);
        }
        CHECK(b.cellRect(ctx, 27).w > b.cellRect(ctx, 1).w);  // „Suche" breiter
    }
}

TEST_CASE_FIXTURE(Fixture, "MainScene: jumpToBucket fokussiert das erste passende Spiel; hasBucket") {
    const Library lib = testLibrary();
    MainScene s(lib);
    for (int i = 0; i < 5; ++i) step(s, action(Action::NextConsole));  // N64 (20 Spiele)
    step(s, InputFrame{});
    s.jumpToBucket('S');
    step(s, InputFrame{}, 300);
    CHECK(std::string(s.infoTitle()).rfind("S", 0) == 0);
    CHECK(s.hasBucket('S'));
    CHECK_FALSE(s.hasBucket('Q'));
    s.jumpToBucket('Q');  // kein Q → nächster folgender Buchstabe (S)
    step(s, InputFrame{});
    CHECK(std::string(s.infoTitle()).rfind("S", 0) == 0);
}

TEST_CASE_FIXTURE(Fixture, "MainScene: Taste Suche öffnet die A–Z-Leiste") {
    const Library lib = testLibrary();
    MainScene s(lib);
    SpyTarget dummy;
    JumpBar bar(s);
    s.setJumpBar(&bar);
    CHECK(step(s, action(Action::Search)).push == &bar);
}
