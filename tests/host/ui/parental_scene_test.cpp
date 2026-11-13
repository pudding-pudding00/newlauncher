#include "ui/parental_scene.hpp"
#include "support/fake_font.hpp"
#include "ui/strings.hpp"
#include <doctest/doctest.h>
#include <array>
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

namespace {
constexpr std::array<u8, 16> kSalt{};
}

TEST_CASE_FIXTURE(Fixture, "Kindersicherung: Werte ändern, B speichert nur bei Änderung") {
    ParentalConfig live = withPin(ParentalConfig{}, "1234", kSalt);
    ParentalScene s;
    s.setConfig(&live);
    s.onEnter();
    CHECK(s.isOverlay());
    CHECK(step(s, Action::Back).command == AppCommand::None);
    s.onEnter();
    step(s, Action::Down);  // System
    step(s, Action::Down);  // Altersgrenze
    const SceneRequests changed = step(s, Action::Right);
    CHECK(changed.command == AppCommand::SettingsChanged);
    CHECK(live.maxAge == 0);  // 18 → Umlauf
    const SceneRequests closed = step(s, Action::Back);
    CHECK(closed.pop);
    CHECK(closed.command == AppCommand::SaveSettings);
}

TEST_CASE_FIXTURE(Fixture, "Kindersicherung: PIN-Zeile meldet ChangePin, Entfernen meldet RemovePin") {
    ParentalConfig live = withPin(ParentalConfig{}, "1234", kSalt);
    ParentalScene s;
    s.setConfig(&live);
    s.onEnter();
    const SceneRequests pin = step(s, Action::Confirm);
    CHECK(pin.command == AppCommand::ChangePin);
    CHECK_FALSE(pin.pop);
    for (int i = 0; i < 5; ++i) step(s, Action::Down);
    CHECK(s.focus() == static_cast<int>(ParentalRow::RemovePin));
    CHECK(step(s, Action::Confirm).command == AppCommand::RemovePin);
    live = withoutPin(live);
    CHECK(step(s, Action::Confirm).command == AppCommand::None);  // ohne PIN ausgegraut
}

TEST_CASE_FIXTURE(Fixture, "Kindersicherung zeichnet in beiden Sprachen") {
    ParentalConfig live;
    ParentalScene s;
    s.setConfig(&live);
    s.onEnter();
    for (Lang l : {Lang::De, Lang::En}) {
        setLanguage(l);
        dl.clear();
        s.draw(ctx, dl);
        CHECK(dl.quadCount() > 0);
        CHECK(dl.droppedQuads() == 0);
    }
    setLanguage(Lang::De);
}
