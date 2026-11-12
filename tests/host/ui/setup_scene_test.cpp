#include "ui/setup_scene.hpp"
#include "ui/strings.hpp"
#include "support/fake_font.hpp"
#include <doctest/doctest.h>
#include <string>
using namespace nl;
using namespace nl::ui;

namespace {
const gfx::AtlasShapes kShapes{gfx::UvRect{0, 0, 0.01f, 0.01f}, gfx::UvRect{0.5f, 0.5f, 0.6f, 0.6f}};

SystemInfo info() {
    SystemInfo s;
    s.emulators = {ResolvedEmuSummary{"nes", "fceugx", "detected", "sd:/apps/fceugx/boot.dol", ""}};
    return s;
}

struct Fixture {
    gfx::FontSet fonts = test::fakeFontSet(8, 16);
    UiContext ctx;
    gfx::DrawList dl{16384, 16};
    SetupScene s;
    Fixture() {
        ctx.theme = &lightTheme();
        ctx.fonts = &fonts;
        ctx.shapes = &kShapes;
        s.start(SetupWizard(info(), Config{}, {}));
        s.onEnter();
    }
    SceneRequests press(Action a) {
        InputFrame f;
        f.actions = 1u << static_cast<u32>(a);
        SceneRequests r;
        s.update(ctx, f, 16, r);
        return r;
    }
};
}  // namespace

TEST_CASE_FIXTURE(Fixture, "Setup: A übernimmt, B überspringt, Minus geht zurück") {
    press(Action::Down);  // Deutsch statt Systemsprache
    press(Action::Confirm);
    CHECK(s.wizard().step() == WizardStep::Sources);
    CHECK(s.wizard().result().language == Config::Language::De);
    press(Action::PrevConsole);
    CHECK(s.wizard().step() == WizardStep::Language);
    press(Action::Back);
    CHECK(s.wizard().step() == WizardStep::Sources);
    CHECK(s.wizard().result().language == Config::Language::Auto);
}

TEST_CASE_FIXTURE(Fixture, "Setup: Netz-Schritt fordert den Verbindungstest einmal an, Fertig fordert Speichern") {
    SceneRequests r;
    int netTests = 0;
    for (int i = 0; i < 4; ++i) {
        r = press(Action::Confirm);
        if (r.command == AppCommand::SetupNetTest) ++netTests;
    }
    REQUIRE(s.wizard().step() == WizardStep::Network);
    CHECK(netTests == 1);
    s.setNetStatus(SetupScene::NetTest::Ok);
    CHECK(s.netStatus() == SetupScene::NetTest::Ok);
    r = press(Action::Confirm);
    CHECK(s.wizard().step() == WizardStep::Done);
    CHECK(r.command == AppCommand::None);
    r = press(Action::Confirm);
    CHECK(r.command == AppCommand::FinishSetup);
    CHECK(r.pop);
    CHECK(std::string(appCommandName(AppCommand::FinishSetup)) == "finish_setup");
    CHECK(std::string(appCommandName(AppCommand::SetupNetTest)) == "setup_net_test");
}

TEST_CASE_FIXTURE(Fixture, "Setup: „Fehlende installieren“ öffnet die Emulator-Verwaltung und bleibt im Schritt") {
    for (int i = 0; i < 3; ++i) press(Action::Confirm);
    REQUIRE(s.wizard().step() == WizardStep::Emulators);
    press(Action::Up);  // Umlauf auf die letzte Option
    REQUIRE(s.wizard().installSelected());
    const SceneRequests r = press(Action::Confirm);
    CHECK(r.command == AppCommand::OpenEmulators);
    CHECK(s.wizard().step() == WizardStep::Emulators);
}

TEST_CASE_FIXTURE(Fixture, "Setup: Home bricht ab, Zeichnen läuft in jedem Schritt") {
    for (int i = 0; i < kWizardStepCount; ++i) {
        s.draw(ctx, dl);
        press(Action::Confirm);
    }
    CHECK(press(Action::Home).pop);
}
