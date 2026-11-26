#include "ui/keyboard.hpp"
#include "core/text_fold.hpp"
#include "gfx/text.hpp"
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

struct SpySearch final : TextTarget {
    std::vector<std::string> queries;
    std::vector<std::string> submitted;
    void setText(std::string_view q) override { queries.emplace_back(q); }
    void submitText(std::string_view q) override { submitted.emplace_back(q); }
};

struct SpyFilter final : FilterSource {
    int count = 3;
    int index = 0;
    int filterCount() const override { return count; }
    int filterIndex() const override { return index; }
    std::string filterLabel(int i) const override { return i == 0 ? "Alle" : i == 1 ? "Favoriten" : "Party"; }
    void setFilterIndex(int i) override { index = i; }
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

// --- M17-Review: Filterzeile auch im Such-Overlay (Spec v2 10.3) ---
TEST_CASE_FIXTURE(Fixture, "Keyboard: Hoch aus der obersten Reihe wählt die Filterzeile, ◀▶ und A blättern mit Umlauf") {
    SpySearch t;
    SpyFilter f;
    Keyboard k(t);
    k.setFilter(&f);
    k.onEnter();
    step(k, action(Action::Up));
    CHECK(k.filterRowFocused());
    step(k, action(Action::Right));
    CHECK(f.index == 1);
    step(k, action(Action::Left));
    step(k, action(Action::Left));
    CHECK(f.index == 2);  // Umlauf
    const SceneRequests r = step(k, action(Action::Confirm));
    CHECK(f.index == 0);  // A blättert weiter, die Tastatur bleibt offen
    CHECK_FALSE(r.pop);
    CHECK(k.text().empty());
    CHECK(t.queries.empty());
    step(k, action(Action::Down));
    CHECK_FALSE(k.filterRowFocused());
    CHECK(k.focusedKey() == k.keyIndex('1'));
    step(k, action(Action::Up));
    k.onEnter();  // neue Suche beginnt auf den Tasten
    CHECK_FALSE(k.filterRowFocused());
}

TEST_CASE_FIXTURE(Fixture, "Keyboard: ohne Filter keine Filterzeile, Hoch bleibt auf den Tasten") {
    SpySearch t;
    SpyFilter f;
    f.count = 0;
    Keyboard k(t);
    k.onEnter();
    step(k, action(Action::Up));
    CHECK_FALSE(k.filterRowFocused());
    k.setFilter(&f);
    step(k, action(Action::Up));
    CHECK_FALSE(k.filterRowFocused());
    CHECK(k.focusedKey() == k.keyIndex('1'));
}

TEST_CASE_FIXTURE(Fixture, "Keyboard: Zeiger auf der Filterzeile, A blättert") {
    SpySearch t;
    SpyFilter f;
    Keyboard k(t);
    k.setFilter(&f);
    k.onEnter();
    tap(k, k.filterRect(ctx));
    CHECK(k.filterRowFocused());
    CHECK(f.index == 1);
    CHECK(k.text().empty());
}

TEST_CASE_FIXTURE(Fixture, "Keyboard: Zeiger auf die Filterzeile spielt den Move-Klang, Verweilen nicht") {
    SpySearch t;
    SpyFilter f;
    Keyboard k(t);
    k.setFilter(&f);
    k.onEnter();
    const gfx::Rect r = k.filterRect(ctx);
    InputFrame in;
    in.pointerValid = true;
    in.pointerMoved = true;
    in.pointerX = r.x + r.w / 2;
    in.pointerY = r.y + r.h / 2;
    CHECK(step(k, in).sound == UiSound::Move);
    CHECK(k.filterRowFocused());
    CHECK(step(k, in).sound == UiSound::None);  // schon dort
}

TEST_CASE_FIXTURE(Fixture, "Keyboard: Filterzeile in 640 und 854 über den Tasten im Bild, ohne verworfene Quads") {
    for (float w : {640.0f, 854.0f}) {
        ctx.width = w;
        SpySearch t;
        SpyFilter f;
        Keyboard k(t);
        k.setFilter(&f);
        k.onEnter();
        step(k, action(Action::Up));
        dl.clear();
        k.draw(ctx, dl);
        CHECK(dl.droppedQuads() == 0);
        const gfx::Rect r = k.filterRect(ctx);
        CHECK(r.x >= 0);
        CHECK(r.x + r.w <= w);
        CHECK(r.y >= 0);
        CHECK(r.y + r.h <= k.keyRect(ctx, 0).y);
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

TEST_CASE_FIXTURE(Fixture, "Keyboard: Klänge – A auf Taste Confirm, Fertig Confirm, Pfeil Move, B Back") {
    SpySearch t;
    Keyboard k(t);
    k.onEnter();
    CHECK(step(k, action(Action::Confirm)).sound == UiSound::Confirm);
    CHECK(step(k, action(Action::Right)).sound == UiSound::Move);
    CHECK(tap(k, k.keyRect(ctx, k.keyIndex(Keyboard::kDone))).sound == UiSound::Confirm);
    CHECK(step(k, action(Action::Back)).sound == UiSound::Back);
}

namespace {
// Tippt eine Folge von Tasten (Zeichen der aktuellen Ebene oder Sondertasten) per Zeiger an.
template <typename F>
void typeKeys(F& fx, Keyboard& k, std::string_view keys) {
    for (char c : keys) {
        const int i = k.keyIndex(c);
        REQUIRE_MESSAGE(i >= 0, "Taste fehlt: ", static_cast<int>(c));
        fx.tap(k, k.keyRect(fx.ctx, i));
    }
}
KeyboardMode codeMode() {
    KeyboardMode m;
    m.maxLen = 128;
    m.layers = true;
    m.hint = "Code";
    return m;
}
}  // namespace

TEST_CASE_FIXTURE(Fixture, "Keyboard: Suche ohne Ebenen bleibt QWERTZ groß mit drei Sondertasten") {
    SpySearch t;
    Keyboard k(t);
    k.onEnter();
    CHECK(k.keyIndex('Q') >= 0);
    CHECK(k.keyIndex('q') < 0);
    CHECK(k.keyIndex(Keyboard::kLayer) < 0);
    CHECK(k.keyCount() == 10 + 10 + 9 + 7 + 3);
}

TEST_CASE_FIXTURE(Fixture, "Keyboard: Ebenen-Taste schaltet abc → ABC → 123 → # → abc") {
    SpySearch t;
    Keyboard k(t);
    k.setMode(codeMode());
    k.onEnter();
    CHECK(k.layerName() == std::string_view("abc"));
    typeKeys(*this, k, "ab");
    typeKeys(*this, k, std::string_view("\x04", 1));
    CHECK(k.layerName() == std::string_view("ABC"));
    CHECK(k.focusedKey() == k.keyIndex(Keyboard::kLayer));  // Fokus bleibt auf der Ebenen-Taste
    typeKeys(*this, k, "XY");
    typeKeys(*this, k, std::string_view("\x04", 1));
    CHECK(k.layerName() == std::string_view("123"));
    typeKeys(*this, k, "7-_.@");
    typeKeys(*this, k, std::string_view("\x04", 1));
    CHECK(k.layerName() == std::string_view("#"));
    typeKeys(*this, k, "!?#$%&*+=/:;,()'\"");
    typeKeys(*this, k, std::string_view("\x04", 1));
    CHECK(k.layerName() == std::string_view("abc"));
    CHECK(k.text() == "abXY7-_.@!?#$%&*+=/:;,()'\"");
    CHECK(t.queries.back() == k.text());
}

TEST_CASE_FIXTURE(Fixture, "Keyboard: Ebenen in 640 und 854 vollständig im Bild, ohne verworfene Quads") {
    SpySearch t;
    Keyboard k(t);
    k.setMode(codeMode());
    k.onEnter();
    for (float w : {640.0f, 854.0f}) {
        ctx.width = w;
        ctx.height = 480;
        for (int layer = 0; layer < 4; ++layer) {
            for (int i = 0; i < k.keyCount(); ++i) {
                const gfx::Rect r = k.keyRect(ctx, i);
                CHECK(r.x >= 0);
                CHECK(r.x + r.w <= w);
            }
            dl.clear();
            k.draw(ctx, dl);
            CHECK(dl.droppedQuads() == 0);
            typeKeys(*this, k, std::string_view("\x04", 1));
        }
    }
}

TEST_CASE_FIXTURE(Fixture, "Keyboard: Längengrenze aus dem Modus (128)") {
    SpySearch t;
    Keyboard k(t);
    k.setMode(codeMode());
    k.onEnter();
    for (int i = 0; i < 130; ++i) typeKeys(*this, k, "a");
    CHECK(k.text().size() == 128);
}

TEST_CASE_FIXTURE(Fixture, "Keyboard: Maskierung zeigt keine Zeichen") {
    SpySearch t;
    Keyboard k(t);
    KeyboardMode m = codeMode();
    m.mask = true;
    k.setMode(m);
    k.onEnter();
    CHECK(k.shownText() == "Code");  // leer → Hinweis
    typeKeys(*this, k, "abc");
    CHECK(k.text() == "abc");
    CHECK(k.shownText() == "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2");
    CHECK(k.shownText().find('a') == std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "Keyboard: Fertig ruft submitText genau einmal und schließt") {
    SpySearch t;
    Keyboard k(t);
    k.setMode(codeMode());
    k.onEnter();
    typeKeys(*this, k, "ok");
    const SceneRequests r = tap(k, k.keyRect(ctx, k.keyIndex(Keyboard::kDone)));
    CHECK(r.pop);
    REQUIRE(t.submitted.size() == 1);
    CHECK(t.submitted[0] == "ok");
    step(k, action(Action::Back));
    CHECK(t.submitted.size() == 1);  // B schließt ohne Einsenden
}

TEST_CASE_FIXTURE(Fixture, "Keyboard: setTarget leitet um, onEnter setzt Ebene zurück") {
    SpySearch a, b;
    Keyboard k(a);
    k.setMode(codeMode());
    k.setTarget(b);
    k.onEnter();
    typeKeys(*this, k, std::string_view("\x04", 1));
    k.onEnter();
    CHECK(k.layerName() == std::string_view("abc"));
    typeKeys(*this, k, "z");
    CHECK(a.queries.empty());
    CHECK(b.queries.size() == 1);
}

TEST_CASE_FIXTURE(Fixture, "Keyboard: requestClose schließt ohne Befehl und Klang, onEnter setzt zurück") {
    SpySearch t;
    Keyboard k(t);
    k.setMode(codeMode());
    k.onEnter();
    k.requestClose();
    const SceneRequests r = step(k, InputFrame{});
    CHECK(r.pop);
    CHECK(r.command == AppCommand::None);
    CHECK(r.sound == UiSound::None);
    k.onEnter();
    CHECK_FALSE(step(k, InputFrame{}).pop);
}

TEST_CASE_FIXTURE(Fixture, "Keyboard: visibleText hält bei langem oder maskiertem Code das Ende sichtbar") {
    SpySearch t;
    Keyboard k(t);
    k.setMode(codeMode());
    k.onEnter();
    for (int i = 0; i < 127; ++i) typeKeys(*this, k, "a");
    typeKeys(*this, k, "z");
    const gfx::FontFace& face = fonts.face(gfx::FontSize::Title, gfx::FontWeight::Medium);
    constexpr float kMaxW = 396.0f;
    const std::string vis = k.visibleText(face, kMaxW);
    CHECK(vis.rfind("\xE2\x80\xA6", 0) == 0);  // beginnt mit …
    CHECK(vis.back() == 'z');
    CHECK(gfx::measureText(face, vis) <= kMaxW);

    KeyboardMode m = codeMode();
    m.mask = true;
    k.setMode(m);
    const std::string masked = k.visibleText(face, kMaxW);
    CHECK(masked.rfind("\xE2\x80\xA6", 0) == 0);
    CHECK(gfx::measureText(face, masked) <= kMaxW);
    // Nur ganze UTF-8-Zeichen (… + N × •, je 3 Byte)
    CHECK((masked.size() % 3) == 0);
    CHECK(masked.find('z') == std::string::npos);
}
