#include "ui/keyboard.hpp"
#include "core/text_fold.hpp"
#include "ui/main_scene.hpp"
#include "ui/test_library.hpp"
#include "support/fake_font.hpp"
#include <doctest/doctest.h>
#include <string>
#include <vector>
using namespace nl;
using namespace nl::ui;

namespace {
const gfx::AtlasShapes kShapes{gfx::UvRect{0, 0, 0.01f, 0.01f}, gfx::UvRect{0.5f, 0.5f, 0.6f, 0.6f}};

struct SpySearch final : SearchTarget {
    std::vector<std::string> queries;
    void setQuery(std::string_view q) override { queries.emplace_back(q); }
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
    template <typename S>
    SceneRequests step(S& s, const InputFrame& in, float dt = 16) {
        SceneRequests r;
        s.update(ctx, in, dt, r);
        return r;
    }
    // Tippt eine Taste per Zeiger an.
    template <typename S>
    SceneRequests tap(S& s, const gfx::Rect& r) {
        InputFrame f;
        f.pointerValid = true;
        f.pointerMoved = true;
        f.pointerX = r.x + r.w / 2;
        f.pointerY = r.y + r.h / 2;
        step(s, f);
        f.pointerMoved = false;
        f.actions = 1u << static_cast<u32>(Action::Confirm);
        return step(s, f);
    }
};
}  // namespace

TEST_CASE_FIXTURE(Fixture, "Keyboard: Tippen, Löschen, Leerzeichen; jede Änderung ruft setQuery") {
    SpySearch t;
    Keyboard k(t);
    k.onEnter();
    tap(k, k.keyRect(ctx, k.keyIndex('M')));
    tap(k, k.keyIndex('A') >= 0 ? k.keyRect(ctx, k.keyIndex('A')) : gfx::Rect{});
    tap(k, k.keyRect(ctx, k.keyIndex(Keyboard::kSpace)));
    tap(k, k.keyRect(ctx, k.keyIndex('6')));
    CHECK(k.text() == "MA 6");
    tap(k, k.keyRect(ctx, k.keyIndex(Keyboard::kBackspace)));
    CHECK(k.text() == "MA ");
    REQUIRE(t.queries.size() == 5);
    CHECK(t.queries.back() == "MA ");
}

TEST_CASE_FIXTURE(Fixture, "Keyboard: höchstens 32 Byte; Löschen auf leerem Text ändert nichts") {
    SpySearch t;
    Keyboard k(t);
    k.onEnter();
    tap(k, k.keyRect(ctx, k.keyIndex(Keyboard::kBackspace)));
    CHECK(t.queries.empty());
    for (int i = 0; i < 40; ++i) tap(k, k.keyRect(ctx, k.keyIndex('X')));
    CHECK(k.text().size() == 32);
    CHECK(t.queries.size() == 32);
}

TEST_CASE_FIXTURE(Fixture, "Keyboard: Steuerkreuz, Hoch/Runter wählt die Taste mit der nächsten Mitte; Ränder klemmen") {
    SpySearch t;
    Keyboard k(t);
    k.onEnter();  // Fokus auf der ersten Taste ("1")
    step(k, action(Action::Up));
    step(k, action(Action::Left));
    CHECK(k.focusedKey() == k.keyIndex('1'));
    step(k, action(Action::Down));  // Q
    step(k, action(Action::Down));  // A
    step(k, action(Action::Down));  // Y (Reihe hat 7 Tasten, nächste Mitte zu A)
    CHECK(k.focusedKey() == k.keyIndex('Y'));
    for (int i = 0; i < 20; ++i) step(k, action(Action::Right));
    CHECK(k.focusedKey() == k.keyIndex('M'));
    step(k, action(Action::Confirm));
    CHECK(k.text() == "M");
    step(k, action(Action::Down));
    step(k, action(Action::Down));  // letzte Reihe klemmt
    CHECK(k.focusedKey() >= k.keyIndex(Keyboard::kSpace));
}

TEST_CASE_FIXTURE(Fixture, "Keyboard: Fertig und Back schließen, Text bleibt") {
    SpySearch t;
    Keyboard k(t);
    k.onEnter();
    tap(k, k.keyRect(ctx, k.keyIndex('Z')));
    CHECK(step(k, action(Action::Back)).pop);
    CHECK(k.text() == "Z");
    CHECK(tap(k, k.keyRect(ctx, k.keyIndex(Keyboard::kDone))).pop);
}

TEST_CASE_FIXTURE(Fixture, "Keyboard: draw in 640 und 854 ohne verworfene Quads, alle Tasten im Bild") {
    for (float w : {640.0f, 854.0f}) {
        ctx.width = w;
        SpySearch t;
        Keyboard k(t);
        k.onEnter();
        dl.clear();
        k.draw(ctx, dl);
        CHECK(dl.droppedQuads() == 0);
        for (int i = 0; i < k.keyCount(); ++i) {
            const gfx::Rect r = k.keyRect(ctx, i);
            CHECK(r.x >= 0);
            CHECK(r.x + r.w <= w);
            CHECK(r.y + r.h <= ctx.height - 72);
        }
    }
}

TEST_CASE_FIXTURE(Fixture, "MainScene: setQuery zeigt Treffer aus allen Konsolen, Such-Eintrag aktiv, Fokus 0") {
    const Library lib = testLibrary();
    MainScene s(lib);
    step(s, InputFrame{});
    s.setQuery("mario");
    step(s, InputFrame{});
    CHECK(s.barIndex() == 0);
    CHECK(s.focusIndex() == 0);
    CHECK(s.grid().count() > 10);
    CHECK(std::string(s.barLabelText(0)) == "Suche: mario");
    bool nes = false, wii = false;
    for (int i = 0; i < s.grid().count(); ++i) {
        s.setFocusForTest(i);
        const std::string title = s.infoTitle();
        CHECK(foldForSearch(title).find("mario") != std::string::npos);
    }
    for (const Game& g : lib.games) {
        if (foldForSearch(g.title).find("mario") == std::string::npos) continue;
        nes |= g.console == Console::Nes;
        wii |= g.console == Console::Wii;
    }
    CHECK(nes);
    CHECK(wii);
}

TEST_CASE_FIXTURE(Fixture, "MainScene: Suche ignoriert Groß/klein und Akzente") {
    const Library lib = testLibrary();
    MainScene s(lib);
    s.setQuery("POKEMON");
    step(s, InputFrame{});
    CHECK(s.grid().count() == 2);
}

TEST_CASE_FIXTURE(Fixture, "MainScene: keine Treffer → kein Fokus, „Keine Treffer\"") {
    const Library lib = testLibrary();
    MainScene s(lib);
    s.setQuery("xyzxyz");
    step(s, InputFrame{});
    CHECK(s.grid().count() == 0);
    CHECK(s.focusIndex() == -1);
    s.draw(ctx, dl);
    CHECK(dl.droppedQuads() == 0);
}

TEST_CASE_FIXTURE(Fixture, "MainScene: Back beendet den Suchmodus und stellt den vorherigen Eintrag wieder her") {
    const Library lib = testLibrary();
    MainScene s(lib);
    for (int i = 0; i < 5; ++i) step(s, action(Action::NextConsole));
    const int before = s.barIndex();
    s.setQuery("zelda");
    step(s, InputFrame{});
    CHECK(s.barIndex() == 0);
    step(s, action(Action::Back));
    CHECK(s.barIndex() == before);
    s.setQuery("");  // leere Anfrage beendet den Suchmodus ebenfalls
    CHECK(s.barIndex() == before);
}

TEST_CASE_FIXTURE(Fixture, "MainScene: Bibliothek wechselt während der Suche → Treffer neu, Anfrage bleibt") {
    const Game a[] = {Game{Console::Nes, "nes/a", "Mario A", 0, 0}, Game{Console::Nes, "nes/b", "Zelda", 0, 0}};
    const Game b[] = {Game{Console::Nes, "nes/a", "Mario A", 0, 0}, Game{Console::Nes, "nes/c", "Mario C", 0, 0}};
    MainScene s(Library{a, {}, {}});
    s.setQuery("mario");
    step(s, InputFrame{});
    CHECK(s.grid().count() == 1);
    s.setLibrary(Library{b, {}, {}}, s.focusedKey());
    step(s, InputFrame{});
    CHECK(s.barIndex() == 0);
    CHECK(s.grid().count() == 2);
    CHECK(std::string(s.barLabelText(0)) == "Suche: mario");
}
