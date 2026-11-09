#include "ui/main_scene.hpp"
#include "ui/test_library.hpp"
#include "support/fake_font.hpp"
#include <doctest/doctest.h>
#include <string>
using namespace nl;
using namespace nl::ui;

namespace {
const gfx::AtlasShapes kShapes{gfx::UvRect{0, 0, 0.01f, 0.01f}, gfx::UvRect{0.5f, 0.5f, 0.6f, 0.6f}};

struct DummyScene final : Scene {
    void update(const UiContext&, const InputFrame&, float, SceneRequests&) override {}
    void draw(const UiContext&, gfx::DrawList&) override {}
    bool isOverlay() const override { return true; }
};

struct Fixture {
    gfx::FontSet fonts = test::fakeFontSet(8, 16);
    UiContext ctx;
    gfx::DrawList dl{4096, 16};
    Fixture() {
        ctx.theme = &lightTheme();
        ctx.fonts = &fonts;
        ctx.shapes = &kShapes;
        ctx.minuteOfDay = 19 * 60 + 42;
    }
    static InputFrame action(Action a) {
        InputFrame f;
        f.actions = 1u << static_cast<u32>(a);
        return f;
    }
    static InputFrame pointerAction(float x, float y, Action a) {
        InputFrame f = pointer(x, y, false);
        f.actions = 1u << static_cast<u32>(a);
        return f;
    }
    static InputFrame pointer(float x, float y, bool moved = true) {
        InputFrame f;
        f.pointerValid = true;
        f.pointerMoved = moved;
        f.pointerX = x;
        f.pointerY = y;
        return f;
    }
    SceneRequests step(MainScene& s, const InputFrame& in, float dt = 16) {
        SceneRequests r;
        s.update(ctx, in, dt, r);
        return r;
    }
};
}  // namespace

TEST_CASE_FIXTURE(Fixture, "Start: Leiste auf dem ersten Eintrag mit Spielen, Fokus 0") {
    const Library lib = testLibrary();
    MainScene s(lib);
    step(s, InputFrame{});
    CHECK(s.barIndex() == 0);
    CHECK(s.focusIndex() == 0);
}

TEST_CASE_FIXTURE(Fixture, "Right/Down bewegen den Fokus; NextConsole wechselt die Liste und setzt Fokus 0") {
    const Library lib = testLibrary();
    MainScene s(lib);
    step(s, action(Action::NextConsole));
    step(s, action(Action::NextConsole));
    CHECK(s.barIndex() == 2);  // NES
    step(s, action(Action::Right));
    step(s, action(Action::Down));
    CHECK(s.focusIndex() == 7);
    step(s, action(Action::NextConsole));
    CHECK(s.barIndex() == 3);
    CHECK(s.focusIndex() == 0);
}

TEST_CASE_FIXTURE(Fixture, "PrevConsole am Anfang und NextConsole am Ende bleiben stehen") {
    const Library lib = testLibrary();
    MainScene s(lib);
    step(s, action(Action::PrevConsole));
    CHECK(s.barIndex() == 0);
    for (int i = 0; i < 20; ++i) step(s, action(Action::NextConsole));
    CHECK(s.barIndex() == 8);
}

TEST_CASE_FIXTURE(Fixture, "Zeiger bewegt → Fokus auf der Kachel unter dem Zeiger") {
    const Library lib = testLibrary();
    MainScene s(lib);
    for (int i = 0; i < 5; ++i) step(s, action(Action::NextConsole));  // N64, 20 Spiele
    const gfx::Rect r = s.grid().tileRect(2, 0);
    step(s, pointer(r.x + r.w / 2, r.y + r.h / 2));
    CHECK(s.focusIndex() == 2);
}

TEST_CASE_FIXTURE(Fixture, "ruhender Zeiger überschreibt den Steuerkreuz-Fokus nicht") {
    const Library lib = testLibrary();
    MainScene s(lib);
    for (int i = 0; i < 5; ++i) step(s, action(Action::NextConsole));
    const gfx::Rect r = s.grid().tileRect(2, 0);
    step(s, pointer(r.x + r.w / 2, r.y + r.h / 2));
    InputFrame f = pointer(r.x + r.w / 2, r.y + r.h / 2, false);
    f.actions = 1u << static_cast<u32>(Action::Right);
    step(s, f);
    CHECK(s.focusIndex() == 3);
    step(s, pointer(r.x + r.w / 2, r.y + r.h / 2, false));
    CHECK(s.focusIndex() == 3);
}

TEST_CASE_FIXTURE(Fixture, "Zeiger bewegt auf leere Fläche → kein Fokus, Infoleiste zeigt Datum") {
    const Library lib = testLibrary();
    MainScene s(lib);
    const gfx::Rect r = s.grid().tileRect(0, 0);
    step(s, pointer(r.x + 10, r.y + 10));  // erst auf eine Kachel zeigen …
    step(s, pointer(r.x - 6, r.y + 10));   // … dann in die Lücke
    CHECK(s.focusIndex() == -1);
    CHECK(std::string(s.infoTitle()).find("November") == std::string::npos);  // Standarddatum: Januar
    CHECK(std::string(s.infoTitle()).find("2026") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "Zeiger erscheint neben den Kacheln: Startfokus bleibt erhalten") {
    const Library lib = testLibrary();
    MainScene s(lib);
    const gfx::Rect r = s.grid().tileRect(5, 0);
    step(s, pointer(r.x + r.w + 40, r.y + 10));
    CHECK(s.focusIndex() == 0);
}

TEST_CASE_FIXTURE(Fixture, "leerer Eintrag (Zuletzt): kein Fokus, Richtungen und Confirm ohne Wirkung") {
    const Library lib = testLibrary();
    MainScene s(lib);
    step(s, action(Action::NextConsole));
    CHECK(s.barIndex() == 1);
    CHECK(s.focusIndex() == -1);
    step(s, action(Action::Right));
    step(s, action(Action::Down));
    SceneRequests r = step(s, action(Action::Confirm));
    CHECK(s.focusIndex() == -1);
    CHECK(r.push == nullptr);
    CHECK(r.command == AppCommand::None);
    s.draw(ctx, dl);
    CHECK(dl.droppedQuads() == 0);
}

TEST_CASE_FIXTURE(Fixture, "leere Bibliothek: draw und update laufen, Text wird gezeichnet") {
    MainScene s(Library{});
    step(s, action(Action::Down));
    step(s, action(Action::NextConsole));
    s.draw(ctx, dl);
    CHECK(dl.quadCount() > 0);
    CHECK(s.focusIndex() == -1);
}

TEST_CASE_FIXTURE(Fixture, "Confirm auf dem Zahnrad liefert CycleTheme und zeigt die Darstellung an") {
    const Library lib = testLibrary();
    MainScene s(lib);
    const gfx::Rect g = s.gearRect(ctx);
    step(s, pointer(g.x + g.w / 2, g.y + g.h / 2));
    SceneRequests r = step(s, pointerAction(g.x + g.w / 2, g.y + g.h / 2, Action::Confirm));
    CHECK(r.command == AppCommand::CycleTheme);
    ctx.themeMode = ThemeMode::Light;
    step(s, InputFrame{});
    CHECK(std::string(s.infoLineText()).find("Darstellung: Hell") != std::string::npos);
    step(s, InputFrame{}, 2100);
    CHECK(std::string(s.infoLineText()).find("Darstellung") == std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "ruhender Zeiger auf dem Zahnrad wirkt auch nach einem Frame ohne Eingaben") {
    const Library lib = testLibrary();
    MainScene s(lib);
    const gfx::Rect g = s.gearRect(ctx);
    step(s, pointer(g.x + g.w / 2, g.y + g.h / 2));
    step(s, InputFrame{});  // z. B. HOME-Menü lag darüber
    CHECK(step(s, pointerAction(g.x + g.w / 2, g.y + g.h / 2, Action::Confirm)).command == AppCommand::CycleTheme);
}

TEST_CASE_FIXTURE(Fixture, "Konsolenleiste nutzt die ganze Breite mit gleichen Abständen") {
    const Library lib = testLibrary();
    for (float w : {640.0f, 854.0f}) {
        ctx.width = w;
        MainScene s(lib);
        step(s, InputFrame{});
        const GridMetrics m = gridMetrics(w);
        CHECK(s.barRect(0).x == doctest::Approx(m.marginX));
        const gfx::Rect last = s.barRect(8);
        CHECK(last.x + last.w == doctest::Approx(w - m.marginX));
        const float gap0 = s.barRect(1).x - (s.barRect(0).x + s.barRect(0).w);
        const float gap5 = s.barRect(6).x - (s.barRect(5).x + s.barRect(5).w);
        CHECK(gap0 == doctest::Approx(gap5));
        CHECK(gap0 > 6.0f);
    }
}

TEST_CASE_FIXTURE(Fixture, "Home fordert push des HOME-Menüs an") {
    const Library lib = testLibrary();
    MainScene s(lib);
    DummyScene home;
    s.setHomeMenu(&home);
    CHECK(step(s, action(Action::Home)).push == &home);
    const gfx::Rect h = s.homeRect(ctx);
    step(s, pointer(h.x + h.w / 2, h.y + h.h / 2));
    CHECK(step(s, pointerAction(h.x + h.w / 2, h.y + h.h / 2, Action::Confirm)).push == &home);
}

TEST_CASE_FIXTURE(Fixture, "Zeiger über einem Leisten-Eintrag + Confirm wählt ihn aus") {
    const Library lib = testLibrary();
    MainScene s(lib);
    step(s, InputFrame{});
    const gfx::Rect b = s.barRect(4);
    step(s, pointer(b.x + b.w / 2, b.y + b.h / 2));
    step(s, pointerAction(b.x + b.w / 2, b.y + b.h / 2, Action::Confirm));
    CHECK(s.barIndex() == 4);
}

TEST_CASE_FIXTURE(Fixture, "draw bleibt innerhalb der DrawList-Kapazität") {
    const Library lib = testLibrary();
    for (float w : {640.0f, 854.0f}) {
        ctx.width = w;
        MainScene s(lib);
        for (int i = 0; i < 5; ++i) step(s, action(Action::NextConsole));
        step(s, InputFrame{}, 300);
        dl.clear();
        s.draw(ctx, dl);
        CHECK(dl.droppedQuads() == 0);
        CHECK(dl.quadCount() < 3000);
        for (const gfx::Vertex& v : dl.vertices()) {
            CHECK(v.x >= -40);
            CHECK(v.x <= w + 40);
        }
    }
}

TEST_CASE_FIXTURE(Fixture, "200 Zeichen langer Titel mit ungültigem UTF-8: Quads bleiben in der Kachel") {
    std::string title(200, 'W');
    title[50] = '\xC3';
    title[120] = '\xFF';
    const Game games[] = {Game{Console::Nes, "x", title.c_str(), 1990, 1}};
    Library lib;
    lib.games = games;
    MainScene s(lib);
    step(s, action(Action::NextConsole));
    step(s, action(Action::NextConsole));
    CHECK(s.barIndex() == 2);
    step(s, action(Action::Left));  // Fokus weg von Kachel 0 geht nicht; Fokus bleibt
    step(s, InputFrame{}, 300);
    // Nur das Raster zeichnen lassen und prüfen, dass der Titeltext die Kachelbreite einhält.
    dl.clear();
    s.drawTile(ctx, dl, 0, s.grid().tileRect(0, 0), false);
    const gfx::Rect r = s.grid().tileRect(0, 0);
    for (const gfx::Vertex& v : dl.vertices()) {
        CHECK(v.x >= r.x - 0.01f);
        CHECK(v.x <= r.x + r.w + 0.01f);
        CHECK(v.y <= r.y + r.h + 0.01f);
    }
}
