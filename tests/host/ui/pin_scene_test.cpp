#include "ui/pin_scene.hpp"
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
    template <class S>
    SceneRequests step(S& s, Action a) {
        SceneRequests r;
        s.update(ctx, action(a), 16, r);
        return r;
    }
    template <class S>
    void enter(S& s, const char* pin) {  // Ziffern mit dem Steuerkreuz, A nach jeder Ziffer
        for (int i = 0; i < 4; ++i) {
            for (int k = 0; k < pin[i] - '0'; ++k) step(s, Action::Up);
            if (i < 3) step(s, Action::Confirm);
        }
    }
};
}  // namespace

TEST_CASE_FIXTURE(Fixture, "PIN-Eingabe: Steuerkreuz wählt Ziffern, A auf der letzten meldet PinEntered") {
    PinScene s;
    s.open(PinMode::Verify);
    CHECK(s.isOverlay());
    CHECK(s.pinText() == "0000");
    enter(s, "1234");
    CHECK(s.cursor() == 3);
    step(s, Action::Down);  // 4 → 3 → wieder 4
    step(s, Action::Up);
    const SceneRequests r = step(s, Action::Confirm);
    CHECK(r.command == AppCommand::PinEntered);
    CHECK_FALSE(r.pop);
    CHECK(s.pinText() == "1234");
    s.accept();
    CHECK(step(s, Action::Up).pop);  // schließt im nächsten Frame
}

TEST_CASE_FIXTURE(Fixture, "PIN-Eingabe: Ziffern laufen um, ◀ geht zurück, reject leert mit Meldung") {
    PinScene s;
    s.open(PinMode::Verify);
    step(s, Action::Down);
    CHECK(s.pinText() == "9000");
    step(s, Action::Right);
    step(s, Action::Up);
    CHECK(s.pinText() == "9100");
    step(s, Action::Left);
    CHECK(s.cursor() == 0);
    s.reject(Str::PinWrong);
    CHECK(s.pinText() == "0000");
    CHECK(s.cursor() == 0);
    CHECK(std::string(s.message()) == tr(Str::PinWrong));
}

TEST_CASE_FIXTURE(Fixture, "PIN-Eingabe: B bricht ab") {
    PinScene s;
    s.open(PinMode::Verify);
    const SceneRequests r = step(s, Action::Back);
    CHECK(r.pop);
    CHECK(r.command == AppCommand::None);
}

TEST_CASE_FIXTURE(Fixture, "Neue PIN: zweimal gleich → PinEntered, verschieden → von vorn") {
    PinScene s;
    s.open(PinMode::Set);
    enter(s, "4711");
    CHECK(step(s, Action::Confirm).command == AppCommand::None);
    CHECK(std::string(s.message()) == tr(Str::PinRepeat));
    CHECK(s.pinText() == "0000");
    enter(s, "4712");
    CHECK(step(s, Action::Confirm).command == AppCommand::None);
    CHECK(std::string(s.message()) == tr(Str::PinMismatch));
    enter(s, "4711");
    step(s, Action::Confirm);
    enter(s, "4711");
    const SceneRequests r = step(s, Action::Confirm);
    CHECK(r.command == AppCommand::PinEntered);
    CHECK(s.pinText() == "4711");
}

TEST_CASE_FIXTURE(Fixture, "PIN-Eingabe zeichnet in beiden Sprachen") {
    PinScene s;
    s.open(PinMode::Set);
    for (Lang l : {Lang::De, Lang::En}) {
        setLanguage(l);
        dl.clear();
        s.draw(ctx, dl);
        CHECK(dl.quadCount() > 0);
        CHECK(dl.droppedQuads() == 0);
    }
    setLanguage(Lang::De);
}

TEST_CASE_FIXTURE(Fixture, "PIN-Eingabe: accept(next) schließt und öffnet die Folgeszene einmal") {
    PinScene s, next;
    s.open(PinMode::Verify);
    s.accept(&next);
    const SceneRequests r = step(s, Action::Up);
    CHECK(r.pop);
    CHECK(r.push == &next);
    s.open(PinMode::Verify);  // open vergisst eine alte Folgeszene
    s.accept();
    CHECK(step(s, Action::Up).push == nullptr);
}
