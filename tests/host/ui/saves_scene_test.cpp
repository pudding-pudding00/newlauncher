#include "ui/saves_scene.hpp"
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
    Fixture() {
        ctx.theme = &lightTheme();
        ctx.fonts = &fonts;
        ctx.shapes = &kShapes;
        ctx.width = 640;
        ctx.height = 480;
    }
    static InputFrame action(Action a) {
        InputFrame f;
        f.actions = 1u << static_cast<u32>(a);
        return f;
    }
    SceneRequests step(SavesScene& s, const InputFrame& in) {
        SceneRequests r;
        s.update(ctx, in, 16, r);
        return r;
    }
};

const Game kGame{Console::Wii, "RMGP01", "Super Mario Galaxy", 2007, 1};

SavesView okView(int backups) {
    SavesView v;
    v.unitId = "wii-RMGP";
    v.location = "nand:/title/00010000/524d4750/data";
    v.bytes = 12345;
    v.files = 2;
    v.state = SavesView::State::Ok;
    for (int i = 0; i < backups; ++i) v.backups.push_back("2026-11-2" + std::to_string(i) + " 10:00");
    return v;
}
}  // namespace

TEST_CASE_FIXTURE(Fixture, "Saves: A auf Sichern meldet BackupSave mit dem Spiel") {
    SavesScene s;
    s.open(kGame, okView(0));
    CHECK(s.isOverlay());
    CHECK(s.focusedButton() == SavesScene::kButtonBackup);
    const SceneRequests r = step(s, action(Action::Confirm));
    CHECK(r.command == AppCommand::BackupSave);
    REQUIRE(r.game != nullptr);
    CHECK(std::string(r.game->key) == "RMGP01");
    CHECK(std::string(appCommandName(AppCommand::BackupSave)) == "backup_save");
}

TEST_CASE_FIXTURE(Fixture, "Saves: ohne Sicherung sind Wiederherstellen und Löschen gesperrt") {
    SavesScene s;
    s.open(kGame, okView(0));
    CHECK(s.buttonEnabled(SavesScene::kButtonBackup));
    CHECK_FALSE(s.buttonEnabled(SavesScene::kButtonRestore));
    CHECK_FALSE(s.buttonEnabled(SavesScene::kButtonDelete));
    step(s, action(Action::Right));
    CHECK(s.focusedButton() == SavesScene::kButtonBackup);  // gesperrte Knöpfe werden übersprungen
    CHECK(s.selectedBackup() == -1);
}

TEST_CASE_FIXTURE(Fixture, "Saves: Hoch/Runter wählt die Sicherung, A auf Wiederherstellen meldet RestoreSave") {
    SavesScene s;
    s.open(kGame, okView(3));
    CHECK(s.selectedBackup() == 0);
    step(s, action(Action::Down));
    step(s, action(Action::Down));
    step(s, action(Action::Down));
    CHECK(s.selectedBackup() == 2);
    step(s, action(Action::Up));
    CHECK(s.selectedBackup() == 1);
    step(s, action(Action::Right));
    CHECK(s.focusedButton() == SavesScene::kButtonRestore);
    SceneRequests r = step(s, action(Action::Confirm));
    CHECK(r.command == AppCommand::RestoreSave);
    step(s, action(Action::Right));
    CHECK(s.focusedButton() == SavesScene::kButtonDelete);
    r = step(s, action(Action::Confirm));
    CHECK(r.command == AppCommand::DeleteBackup);
}

TEST_CASE_FIXTURE(Fixture, "Saves: NAND gesperrt und unbekannter Ort zeigen nur Text, keine Knöpfe") {
    for (auto st : {SavesView::State::NandLocked, SavesView::State::Unknown}) {
        SavesScene s;
        SavesView v = okView(2);
        v.state = st;
        s.open(kGame, v);
        for (int b = 0; b < SavesScene::kButtonCount; ++b) CHECK_FALSE(s.buttonEnabled(b));
        CHECK(s.focusedButton() == -1);
        CHECK(step(s, action(Action::Confirm)).command == AppCommand::None);
        CHECK(std::string(s.stateText()) ==
              tr(st == SavesView::State::NandLocked ? Str::SavesNandLocked : Str::SavesUnknown));
    }
}

TEST_CASE_FIXTURE(Fixture, "Saves: leerer Spielstand – Sichern gesperrt, Wiederherstellen mit Sicherung erlaubt") {
    SavesScene s;
    SavesView v = okView(1);
    v.state = SavesView::State::Empty;
    s.open(kGame, v);
    CHECK_FALSE(s.buttonEnabled(SavesScene::kButtonBackup));
    CHECK(s.buttonEnabled(SavesScene::kButtonRestore));
    CHECK(s.focusedButton() == SavesScene::kButtonRestore);
    CHECK(std::string(s.stateText()) == tr(Str::SavesEmpty));
}

TEST_CASE_FIXTURE(Fixture, "Saves: während eines Auftrags ist alles gesperrt; setView gibt wieder frei") {
    SavesScene s;
    SavesView v = okView(1);
    v.busy = true;
    s.open(kGame, v);
    CHECK(step(s, action(Action::Confirm)).command == AppCommand::None);
    v.busy = false;
    v.backups.clear();
    s.setView(v);
    CHECK(s.selectedBackup() == -1);
    CHECK(step(s, action(Action::Confirm)).command == AppCommand::BackupSave);
}

TEST_CASE_FIXTURE(Fixture, "Saves: B schließt; Meldung bleibt bis open(); zeichnet ohne Absturz") {
    SavesScene s;
    s.open(kGame, okView(9));
    s.setStatus("Fertig");
    CHECK(s.status() == "Fertig");
    gfx::DrawList dl{8192, 16};
    s.draw(ctx, dl);
    CHECK(step(s, action(Action::Back)).pop);
    s.open(kGame, okView(0));
    CHECK(s.status().empty());
}
