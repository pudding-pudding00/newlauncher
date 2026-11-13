#include "ui/options_scene.hpp"
#include "ui/strings.hpp"
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
    OptionsScene s;
    Lang saved = language();
    Fixture() {
        ctx.theme = &lightTheme();
        ctx.fonts = &fonts;
        ctx.shapes = &kShapes;
        setLanguage(Lang::De);
    }
    ~Fixture() { setLanguage(saved); }
    void open(OptionsModel m, std::string title = {}) {
        s.open(std::move(m), std::move(title));
        s.onEnter();
    }
    static InputFrame action(Action a) {
        InputFrame f;
        f.actions = 1u << static_cast<u32>(a);
        return f;
    }
    SceneRequests press(Action a) {
        SceneRequests r;
        s.update(ctx, action(a), 16, r);
        return r;
    }
    int rowOf(std::string_view key) {
        for (std::size_t r = 0; r < s.model().rows(); ++r)
            if (s.model().key(r) == key) return static_cast<int>(r);
        FAIL("Zeile fehlt");
        return 0;
    }
    void focus(int row) {
        while (s.focus() < row) press(Action::Down);
        REQUIRE(s.focus() == row);
    }
};
}  // namespace

TEST_CASE_FIXTURE(Fixture, "Optionen: geerbter Wert zeigt Standard (…), Befehlsnamen") {
    open(OptionsModel(Console::Wii, "RMGP01", "[wii]\nvideo = pal60\n", ""), "Super Mario Galaxy");
    CHECK(s.isOverlay());
    CHECK(s.rowCount() == 16);  // 15 Optionen + Zurücksetzen
    CHECK(s.valueText(rowOf("video")) == "Standard (PAL 60)");
    CHECK(s.labelText(rowOf("video")) == "Videomodus");
    CHECK(s.labelText(15) == "Auf Standard zurücksetzen");
    CHECK(s.labelText(rowOf("cheats")) == "Cheats");
    CHECK(s.labelText(rowOf("hook")) == "Cheat-Hook");
    CHECK(s.valueText(rowOf("hook")) == "Standard (VBI)");
    setLanguage(Lang::En);
    CHECK(s.valueText(rowOf("video")) == "Default (PAL 60)");
    CHECK(s.labelText(rowOf("hook")) == "Cheat hook");
    CHECK(std::string(appCommandName(AppCommand::OpenOptions)) == "open_options");
    CHECK(std::string(appCommandName(AppCommand::SaveOptions)) == "save_options");
}

TEST_CASE_FIXTURE(Fixture, "Optionen global: kein Standard (…), Titel nach Konsole") {
    open(OptionsModel(Console::Gc, "", "", ""));
    CHECK(s.rowCount() == 12);
    CHECK(s.valueText(rowOf("video")) == "Automatisch");
    CHECK(s.valueText(rowOf("memcard_blocks")) == "251 Blöcke");
    CHECK(s.valueText(rowOf("progressive")) == "Aus");
    CHECK(s.titleText() == "GameCube-Spiele");
    CHECK(s.labelText(11) == "Zurücksetzen");
}

TEST_CASE_FIXTURE(Fixture, "Optionen: ◀▶ ändert, B speichert nur bei Änderung") {
    open(OptionsModel(Console::Wii, "RMGP01", "", ""));
    SceneRequests r = press(Action::Back);
    CHECK(r.pop);
    CHECK(r.command == AppCommand::None);

    open(OptionsModel(Console::Wii, "RMGP01", "", ""));
    focus(rowOf("aspect"));
    press(Action::Right);
    CHECK(s.valueText(rowOf("aspect")) == "Spiel");
    press(Action::Right);
    CHECK(s.valueText(rowOf("aspect")) == "4:3");
    r = press(Action::Back);
    CHECK(r.pop);
    CHECK(r.command == AppCommand::SaveOptions);
    CHECK(s.model().gamesIniText() == "[game RMGP01]\naspect = 4:3\n");
}

TEST_CASE_FIXTURE(Fixture, "Optionen: A auf Zurücksetzen entfernt den Spielabschnitt") {
    open(OptionsModel(Console::Wii, "RMGP01", "", "[game RMGP01]\nvideo = ntsc\n"));
    CHECK(s.valueText(rowOf("video")) == "NTSC");
    focus(15);
    press(Action::Confirm);
    CHECK(s.model().gamesIniText().empty());
    CHECK(s.valueText(rowOf("video")) == "Standard (System)");
    CHECK(press(Action::Back).command == AppCommand::SaveOptions);
}

TEST_CASE_FIXTURE(Fixture, "Optionen: gesperrte Zeile nennt die Nintendont-Version und bleibt") {
    open(OptionsModel(Console::Gc, "GM8P01", "", "", NinVersion{4, 400}));
    const int r = rowOf("skip_ipl");
    CHECK(s.valueText(r) == "ab Nintendont 4.434");
    focus(r);
    press(Action::Right);
    CHECK_FALSE(s.model().dirty());
}

TEST_CASE_FIXTURE(Fixture, "Optionen: Karte passt in 480 Zeilen, Zeichnen ohne verworfene Quads") {
    open(OptionsModel(Console::Wii, "RMGP01", "", ""), std::string(300, 'x'));
    ctx.height = 480.0f;
    const gfx::Rect last = s.rowRect(ctx, s.rowCount() - 1);
    CHECK(last.y + last.h + 24.0f + 16.0f <= 480.0f + 0.5f);
    s.draw(ctx, dl);
    CHECK(dl.droppedQuads() == 0);
}

TEST_CASE_FIXTURE(Fixture, "Optionen: requestClose schließt im nächsten Frame und speichert bei Änderung") {
    open(OptionsModel(Console::Wii, "RMGP01", "", ""));
    REQUIRE(s.model().set("video", "pal60"));
    s.requestClose();
    SceneRequests r;
    s.update(ctx, InputFrame{}, 16, r);
    CHECK(r.pop);
    CHECK(r.command == AppCommand::SaveOptions);
}
