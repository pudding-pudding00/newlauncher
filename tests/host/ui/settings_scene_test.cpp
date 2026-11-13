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

TEST_CASE_FIXTURE(Fixture, "A auf Emulatoren öffnet die Emulator-Verwaltung") {
    focusRow(SettingRow::Emulators);
    const SceneRequests r = press(Action::Confirm);
    CHECK(r.pop);
    CHECK(r.command == AppCommand::OpenEmulators);
}

TEST_CASE_FIXTURE(Fixture, "A auf Über öffnet die Über-Seite (M11)") {
    focusRow(SettingRow::About);
    const SceneRequests r = press(Action::Confirm);
    CHECK(r.pop);
    CHECK(r.command == AppCommand::OpenAbout);
    CHECK(std::string(appCommandName(AppCommand::OpenAbout)) == "open_about");
}

TEST_CASE_FIXTURE(Fixture, "A auf Systeminfo öffnet die Systeminfo (M13)") {
    focusRow(SettingRow::SysInfo);
    const SceneRequests r = press(Action::Confirm);
    CHECK(r.pop);
    CHECK(r.command == AppCommand::OpenSysInfo);
    CHECK(std::string(appCommandName(AppCommand::OpenSysInfo)) == "open_sysinfo");
    CHECK(std::string(appCommandName(AppCommand::SaveSysInfo)) == "save_sysinfo");
}

TEST_CASE_FIXTURE(Fixture, "A auf Einrichtung öffnet den Assistenten (M13)") {
    focusRow(SettingRow::Setup);
    const SceneRequests r = press(Action::Confirm);
    CHECK(r.pop);
    CHECK(r.command == AppCommand::OpenSetup);
    CHECK(std::string(appCommandName(AppCommand::OpenSetup)) == "open_setup");
}

TEST_CASE_FIXTURE(Fixture, "Karte mit allen Zeilen passt in 480 Zeilen") {
    ctx.height = 480.0f;
    const gfx::Rect first = s.rowRect(ctx, 0);
    const gfx::Rect last = s.rowRect(ctx, SettingsScene::kCount - 1);
    CHECK(first.y >= 0.0f);
    CHECK(last.y + last.h + 24.0f + 16.0f <= 480.0f + 0.5f);
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
    cfg.libraryRules.sources = SourceSpec{false, {"usb2", "sd"}};  // Hinweis „nach Neustart“ und eigene Liste mitzeichnen
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

TEST_CASE_FIXTURE(Fixture, "Klänge: Wert ändern Confirm, Fokus runter Move, ausgegraut still, B Back, Aktion Confirm") {
    CHECK(press(Action::Right).sound == UiSound::Confirm);
    CHECK(press(Action::Down).sound == UiSound::Move);
    CHECK(press(Action::Right).sound == UiSound::Confirm);  // Theme
    press(Action::Left);
    CHECK(press(Action::Down).sound == UiSound::Move);       // DarkFrom: ausgegraut bei Theme hell
    CHECK(press(Action::Right).sound == UiSound::None);
    CHECK(press(Action::Back).sound == UiSound::Back);
    s.onEnter();
    focusRow(SettingRow::VcMap);
    CHECK(press(Action::Confirm).sound == UiSound::Confirm);
}

TEST_CASE_FIXTURE(Fixture, "Hinweis „nach Neustart“ vergleicht mit den Quellen beim Start, nicht beim Öffnen") {
    CHECK_FALSE(s.restartHintShown());  // auto, gestartet mit auto
    focusRow(SettingRow::Library);
    press(Action::Right);  // Auto → USB
    CHECK(s.restartHintShown());
    press(Action::Back);   // gespeichert, geschlossen
    s.onEnter();           // wieder geöffnet: Neustart steht weiter aus
    CHECK(s.restartHintShown());
    focusRow(SettingRow::Library);
    press(Action::Left);   // zurück auf auto
    CHECK_FALSE(s.restartHintShown());
}

TEST_CASE_FIXTURE(Fixture, "Hinweis „nach Neustart“ nach USB-Rückfall auf SD") {
    cfg.libraryRules.sources = SourceSpec{false, {"usb"}};
    s.setActiveSources(SourceSpec{false, {"sd"}});  // config.ini sagt USB, gestartet wurde mit SD
    CHECK(s.restartHintShown());
    focusRow(SettingRow::Library);
    press(Action::Right);  // USB → SD, also das laufende Gerät
    CHECK_FALSE(s.restartHintShown());
}

TEST_CASE_FIXTURE(Fixture, "Bibliothekszeile zeigt Automatisch statt USB, wenn sources = auto") {
    setLanguage(Lang::De);
    CHECK(s.valueTextFor(SettingRow::Library) == "Automatisch");
    cfg.libraryRules.sources = SourceSpec{false, {"usb2", "sd"}};
    CHECK(s.valueTextFor(SettingRow::Library) == "usb2,sd");
}

TEST_CASE_FIXTURE(Fixture, "Wii-Spiele und GameCube-Spiele öffnen die globalen Optionen (M14)") {
    CHECK(isAction(SettingRow::WiiGames));
    CHECK(isAction(SettingRow::GcGames));
    focusRow(SettingRow::WiiGames);
    SceneRequests r = press(Action::Confirm);
    CHECK(r.command == AppCommand::OpenOptions);
    CHECK(r.game == nullptr);
    CHECK(r.console == Console::Wii);
    CHECK(r.pop);
    s.onEnter();
    focusRow(SettingRow::GcGames);
    r = press(Action::Confirm);
    CHECK(r.command == AppCommand::OpenOptions);
    CHECK(r.console == Console::Gc);
}

TEST_CASE_FIXTURE(Fixture, "A auf Kindersicherung meldet OpenParental (M17)") {
    focusRow(SettingRow::Parental);
    const SceneRequests r = press(Action::Confirm);
    CHECK(r.pop);
    CHECK(r.command == AppCommand::OpenParental);
    CHECK(std::string(appCommandName(AppCommand::OpenParental)) == "open_parental");
}
