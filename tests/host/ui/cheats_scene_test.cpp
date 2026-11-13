#include "ui/cheats_scene.hpp"
#include "support/fake_font.hpp"
#include "ui/strings.hpp"
#include <doctest/doctest.h>
#include <string>
using namespace nl;
using namespace nl::ui;

namespace {
const gfx::AtlasShapes kShapes{gfx::UvRect{0, 0, 0.01f, 0.01f}, gfx::UvRect{0.5f, 0.5f, 0.6f, 0.6f}};

constexpr std::string_view kTxt =
    "RMCP01\nMario Kart Wii\n\n"
    "Alpha [Ich]\n04000000 00000001\n\n"
    "Platzhalter\n04000000 0000XXXX\n\n"
    "Beta\n04000004 00000002\n";

CheatModel model(std::string_view sel = "") { return CheatModel(Console::Wii, kTxt, sel, true); }

struct Fixture {
    gfx::FontSet fonts = test::fakeFontSet(8, 16);
    UiContext ctx;
    gfx::DrawList dl{8192, 16};
    CheatsScene s;
    Fixture() {
        ctx.theme = &lightTheme();
        ctx.fonts = &fonts;
        ctx.shapes = &kShapes;
        setLanguage(Lang::De);
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

TEST_CASE_FIXTURE(Fixture, "Cheats: A schaltet, Platzhalter bleibt aus, B speichert nur bei Änderung") {
    s.open(model(), "RMCP01", "Mario Kart Wii", true, std::nullopt);
    CHECK(s.focus() == 0);
    CHECK(press(Action::Back).command == AppCommand::None);

    s.open(model(), "RMCP01", "Mario Kart Wii", true, std::nullopt);
    press(Action::Confirm);
    CHECK(s.model().checked(0));
    press(Action::Down);
    press(Action::Confirm);
    CHECK_FALSE(s.model().checked(1));
    CHECK(s.rowText(1).find("nicht wählbar") != std::string::npos);
    SceneRequests r = press(Action::Back);
    CHECK(r.pop);
    CHECK(r.command == AppCommand::SaveCheats);
    CHECK(s.model().selText() == "Alpha\n");
}

TEST_CASE_FIXTURE(Fixture, "Cheats: +/R ohne Datei lädt sofort, mit Datei erst nach Nachfrage") {
    s.open(CheatModel(Console::Gc, "", "", true), "GM8P01", "Metroid", false, std::nullopt);
    CHECK(s.statusText() == tr(Str::CheatsNoFile));
    SceneRequests r = press(Action::Options);
    CHECK(r.command == AppCommand::FetchCheats);
    CHECK_FALSE(s.overwrite());
    CHECK(s.fetching());
    CHECK(s.statusText() == tr(Str::CheatsLoading));
    CHECK(press(Action::Options).command == AppCommand::None);  // läuft schon

    s.open(model(), "RMCP01", "Mario Kart Wii", true, std::nullopt);
    CHECK(press(Action::Options).command == AppCommand::None);
    CHECK(s.confirmingOverwrite());
    CHECK(press(Action::Back).pop == false);  // B bricht nur die Nachfrage ab
    CHECK_FALSE(s.confirmingOverwrite());
    press(Action::Options);
    r = press(Action::Confirm);
    CHECK(r.command == AppCommand::FetchCheats);
    CHECK(s.overwrite());
}

TEST_CASE_FIXTURE(Fixture, "Cheats: Ergebnis des Ladens") {
    s.open(CheatModel(Console::Gc, "", "", true), "GM8P01", "Metroid", false, std::nullopt);
    press(Action::Options);
    s.fetchDone(CheatFetchResult::NoCodes, std::nullopt);
    CHECK_FALSE(s.fetching());
    CHECK(s.statusText() == "Keine Codes für dieses Spiel gefunden");
    press(Action::Options);
    s.fetchDone(CheatFetchResult::NotFound, std::nullopt);
    CHECK(s.statusText() == tr(Str::CheatsNoCodes));
    press(Action::Options);
    s.fetchDone(CheatFetchResult::NetError, std::nullopt);
    CHECK(s.statusText() == tr(Str::CheatsNetError));
    press(Action::Options);
    s.fetchDone(CheatFetchResult::Ok, model("Beta\n"));
    CHECK(s.model().rows() == 3);
    CHECK(s.model().checked(2));
    CHECK(s.statusText().empty());
}

TEST_CASE_FIXTURE(Fixture, "Cheats: Laden gesperrt nennt den Grund; Wii ohne Cheats lädt nicht") {
    s.open(CheatModel(Console::Gc, "", "", true), "GM8P01", "Metroid", false, Str::CheatsNoSource);
    CHECK(press(Action::Options).command == AppCommand::None);
    CHECK(s.statusText() == tr(Str::CheatsNoSource));

    s.open(CheatModel(Console::Wii, kTxt, "", false), "RMCP01", "Mario Kart Wii", true, std::nullopt);
    CHECK(s.statusText() == "Wii-Cheats in dieser Version nicht verfügbar");
    CHECK(press(Action::Options).command == AppCommand::None);
    CHECK(press(Action::Confirm).command == AppCommand::None);
}

TEST_CASE_FIXTURE(Fixture, "Cheats: lange Liste blättert, Zeichnen bleibt im Bild") {
    std::string txt = "GM8P01\nMetroid\n";
    for (int i = 0; i < 40; ++i) txt += "\nCode " + std::to_string(i) + "\n00000000 0000000" + std::to_string(i % 10) + "\n";
    s.open(CheatModel(Console::Gc, txt, "", true), "GM8P01", "Metroid", true, std::nullopt);
    CHECK(s.model().rows() == 40);
    for (int i = 0; i < 39; ++i) press(Action::Down);
    CHECK(s.focus() == 39);
    press(Action::Down);
    CHECK(s.focus() == 39);
    press(Action::Left);
    CHECK(s.focus() == 39 - CheatsScene::kVisibleRows);
    press(Action::Right);
    CHECK(s.focus() == 39);
    s.draw(ctx, dl);
    CHECK(dl.droppedQuads() == 0);
}

TEST_CASE_FIXTURE(Fixture, "Cheats: Befehlsnamen und englische Texte") {
    CHECK(std::string(appCommandName(AppCommand::FetchCheats)) == "fetch_cheats");
    CHECK(std::string(appCommandName(AppCommand::SaveCheats)) == "save_cheats");
    setLanguage(Lang::En);
    CHECK(std::string(tr(Str::CheatsNoCodes)) == "No codes found for this game");
    setLanguage(Lang::De);
}
