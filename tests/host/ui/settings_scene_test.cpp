#include "ui/settings_scene.hpp"
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
    Config cfg;
    SettingsScene s;
    Fixture() {
        ctx.theme = &lightTheme();
        ctx.fonts = &fonts;
        ctx.shapes = &kShapes;
        s.setConfig(&cfg);
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
    void focusRow(SettingRow row) {
        for (int i = 0; i < static_cast<int>(row); ++i) press(Action::Down);
        REQUIRE(s.focus() == static_cast<int>(row));
    }
};
}  // namespace

TEST_CASE_FIXTURE(Fixture, "Overlay, Fokus 0 nach onEnter, Befehlsnamen") {
    CHECK(s.isOverlay());
    CHECK(s.focus() == 0);
    CHECK(std::string(appCommandName(AppCommand::SettingsChanged)) == "settings_changed");
    CHECK(std::string(appCommandName(AppCommand::SaveSettings)) == "save_settings");
    CHECK(std::string(appCommandName(AppCommand::Rescan)) == "rescan");
}

TEST_CASE_FIXTURE(Fixture, "rechts auf Sprache ändert Config und meldet SettingsChanged") {
    const SceneRequests r = press(Action::Right);
    CHECK(cfg.language == Config::Language::De);
    CHECK(r.command == AppCommand::SettingsChanged);
    CHECK_FALSE(r.pop);
    press(Action::Left);
    CHECK(cfg.language == Config::Language::Auto);
}

TEST_CASE_FIXTURE(Fixture, "A auf einer Werte-Zeile schaltet weiter") {
    focusRow(SettingRow::Sounds);
    const SceneRequests r = press(Action::Confirm);
    CHECK_FALSE(cfg.sounds);
    CHECK(r.command == AppCommand::SettingsChanged);
    CHECK_FALSE(r.pop);
}

TEST_CASE_FIXTURE(Fixture, "B ohne Änderung schließt ohne SaveSettings") {
    const SceneRequests r = press(Action::Back);
    CHECK(r.pop);
    CHECK(r.command == AppCommand::None);
}

TEST_CASE_FIXTURE(Fixture, "B nach Änderung meldet SaveSettings") {
    press(Action::Right);
    const SceneRequests r = press(Action::Back);
    CHECK(r.pop);
    CHECK(r.command == AppCommand::SaveSettings);
}

TEST_CASE_FIXTURE(Fixture, "B nach Hin- und Zurückschalten speichert nicht") {
    press(Action::Right);
    press(Action::Left);
    CHECK(press(Action::Back).command == AppCommand::None);
}

TEST_CASE_FIXTURE(Fixture, "Lautstärke ausgegraut bei Musik aus") {
    cfg.music = false;
    s.onEnter();
    focusRow(SettingRow::Volume);
    const SceneRequests r = press(Action::Right);
    CHECK(cfg.musicVolume == Config{}.musicVolume);
    CHECK(r.command == AppCommand::None);
    press(Action::Down);
    CHECK(s.focus() == static_cast<int>(SettingRow::Sounds));  // Fokus überspringt nichts
}

TEST_CASE_FIXTURE(Fixture, "A auf VC-Injects zuordnen ohne Änderungen") {
    focusRow(SettingRow::VcMap);
    const SceneRequests r = press(Action::Confirm);
    CHECK(r.pop);
    CHECK(r.command == AppCommand::OpenVcMap);
}

TEST_CASE_FIXTURE(Fixture, "A auf Fehlende Cover laden") {
    focusRow(SettingRow::FetchCovers);
    const SceneRequests r = press(Action::Confirm);
    CHECK(r.pop);
    CHECK(r.command == AppCommand::FetchCovers);
}

TEST_CASE_FIXTURE(Fixture, "A auf Bibliothek neu scannen nach Änderung") {
    press(Action::Right);
    focusRow(SettingRow::Rescan);
    const SceneRequests first = press(Action::Confirm);
    CHECK(first.command == AppCommand::SaveSettings);
    CHECK_FALSE(first.pop);
    SceneRequests second;
    s.update(ctx, InputFrame{}, 16, second);
    CHECK(second.pop);
    CHECK(second.command == AppCommand::Rescan);
}

TEST_CASE_FIXTURE(Fixture, "Zeiger auf Zeile 3 setzt Fokus 3") {
    const gfx::Rect r = s.rowRect(ctx, 3);
    InputFrame f;
    f.pointerValid = true;
    f.pointerMoved = true;
    f.pointerX = r.x + 5;
    f.pointerY = r.y + r.h / 2;
    SceneRequests out;
    s.update(ctx, f, 16, out);
    CHECK(s.focus() == 3);
}

TEST_CASE_FIXTURE(Fixture, "kein Umlauf am Listenrand") {
    press(Action::Up);
    CHECK(s.focus() == 0);
    for (int i = 0; i < 20; ++i) press(Action::Down);
    CHECK(s.focus() == static_cast<int>(SettingRow::Count) - 1);
}

TEST_CASE_FIXTURE(Fixture, "draw in 640 und 854, beide Sprachen: keine verworfenen Quads, alles im Bild") {
    cfg.library = Config::LibraryDevice::Sd;  // Hinweis „nach Neustart“ mitzeichnen
    for (Lang lang : {Lang::De, Lang::En}) {
        setLanguage(lang);
        for (float w : {640.0f, 854.0f}) {
            ctx.width = w;
            dl.clear();
            s.draw(ctx, dl);
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
    setLanguage(Lang::De);
}
