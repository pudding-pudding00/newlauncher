#include "ui/emulators_scene.hpp"
#include "ui/strings.hpp"
#include "support/fake_font.hpp"
#include <doctest/doctest.h>
#include <string>
using namespace nl;
using namespace nl::ui;

namespace {
const gfx::AtlasShapes kShapes{gfx::UvRect{0, 0, 0.01f, 0.01f}, gfx::UvRect{0.5f, 0.5f, 0.6f, 0.6f}};

OscApp remote(std::string name, u32 ts) {
    OscApp a;
    a.name = std::move(name);
    a.timestamp = ts;
    a.version = "9.9";
    return a;
}

// fceugx: Update verfügbar; snes9xgx: Version unbekannt; vbagx: nicht installiert (alle im Repo);
// not64: aktuell; wii64 und Nintendont fehlen im Repo.
std::vector<EmuRow> sampleRows() {
    std::map<std::string, InstalledEmu> inst;
    inst["fceugx"] = InstalledEmu{1, "1.0"};
    inst["not64"] = InstalledEmu{50, "2.0"};
    const std::vector<OscApp> list = {remote("fceugx", 10), remote("snes9xgx", 10), remote("vbagx", 10),
                                      remote("not64", 50)};
    auto exists = [](std::string_view d) {
        return d == "sd:/apps/fceugx/boot.dol" || d == "sd:/apps/snes9xgx/boot.dol" || d == "sd:/apps/not64/boot.dol";
    };
    return emuRows(inst, exists, list);
}

struct Fixture {
    gfx::FontSet fonts = test::fakeFontSet(8, 16);
    UiContext ctx;
    gfx::DrawList dl{16384, 16};
    EmulatorsScene s;
    Fixture() {
        ctx.theme = &lightTheme();
        ctx.fonts = &fonts;
        ctx.shapes = &kShapes;
        s.onEnter();
        s.setRows(sampleRows());
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
    void focusRow(int row) {
        for (int i = 0; i < row; ++i) press(Action::Down);
        REQUIRE(s.focus() == row);
    }
};
}  // namespace

TEST_CASE_FIXTURE(Fixture, "6 Emulatorzeilen und 3 Aktionen, Overlay") {
    CHECK(s.isOverlay());
    CHECK(EmulatorsScene::kRowCount == 9);
    CHECK(EmulatorsScene::kRowCheckUpdates == 6);
    CHECK(EmulatorsScene::kRowInstallAll == 7);
    CHECK(EmulatorsScene::kRowUpdateAll == 8);
    REQUIRE(s.selectedRow() != nullptr);
    CHECK(s.selectedRow()->info->oscName == "fceugx");
    focusRow(8);
    CHECK(s.selectedRow() == nullptr);
    press(Action::Down);
    CHECK(s.focus() == 8);
    s.draw(ctx, dl);
    CHECK(std::string(appCommandName(AppCommand::OpenEmulators)) == "open_emulators");
    CHECK(std::string(appCommandName(AppCommand::EmuInstallAll)) == "emu_install_all");
}

TEST_CASE_FIXTURE(Fixture, "A auf Emulatorzeile ohne geladene Liste löst nichts aus") {
    const SceneRequests r = press(Action::Confirm);
    CHECK(r.command == AppCommand::None);
    CHECK_FALSE(r.pop);
}

TEST_CASE_FIXTURE(Fixture, "A auf Emulatorzeile nach geladener Liste → EmuInstall mit genau dieser App") {
    s.setListState(EmulatorsScene::ListState::Loaded);
    const SceneRequests r = press(Action::Confirm);
    CHECK(r.command == AppCommand::EmuInstall);
    CHECK_FALSE(r.pop);
    const auto t = s.takeTargets();
    REQUIRE(t.size() == 1);
    CHECK(t[0].name == "fceugx");
    CHECK(s.takeTargets().empty());
}

TEST_CASE_FIXTURE(Fixture, "A auf Emulator, der nicht im Repo ist → nichts") {
    s.setListState(EmulatorsScene::ListState::Loaded);
    focusRow(4);  // wii64
    CHECK(press(Action::Confirm).command == AppCommand::None);
}

TEST_CASE_FIXTURE(Fixture, "Nach Updates suchen → EmuFetchList") {
    focusRow(EmulatorsScene::kRowCheckUpdates);
    const SceneRequests r = press(Action::Confirm);
    CHECK(r.command == AppCommand::EmuFetchList);
    CHECK_FALSE(r.pop);
}

TEST_CASE_FIXTURE(Fixture, "Alle installieren nimmt nur nicht installierte Apps im Repo") {
    s.setListState(EmulatorsScene::ListState::Loaded);
    focusRow(EmulatorsScene::kRowInstallAll);
    const SceneRequests r = press(Action::Confirm);
    CHECK(r.command == AppCommand::EmuInstallAll);
    const auto t = s.takeTargets();
    REQUIRE(t.size() == 1);
    CHECK(t[0].name == "vbagx");

    auto rows = sampleRows();
    rows[2].state = EmuState::Current;
    s.setRows(std::move(rows));
    CHECK(press(Action::Confirm).command == AppCommand::None);
}

TEST_CASE_FIXTURE(Fixture, "Alle aktualisieren nimmt Updates und unbekannte Versionen, keine neuen") {
    s.setListState(EmulatorsScene::ListState::Loaded);
    focusRow(EmulatorsScene::kRowUpdateAll);
    const SceneRequests r = press(Action::Confirm);
    CHECK(r.command == AppCommand::EmuInstallAll);
    const auto t = s.takeTargets();
    REQUIRE(t.size() == 2);
    CHECK(t[0].name == "fceugx");
    CHECK(t[1].name == "snes9xgx");
}

TEST_CASE_FIXTURE(Fixture, "Alle installieren ohne geladene Liste lädt zuerst die Liste und liefert danach die fehlenden Ziele") {
    focusRow(EmulatorsScene::kRowInstallAll);
    const SceneRequests r = press(Action::Confirm);
    CHECK(r.command == AppCommand::EmuFetchList);
    CHECK(s.takePendingTargets().empty());

    s.setListState(EmulatorsScene::ListState::Loaded);
    const auto t = s.takePendingTargets();
    REQUIRE(t.size() == 1);
    CHECK(t[0].name == "vbagx");
    CHECK(s.takePendingTargets().empty());
}

TEST_CASE_FIXTURE(Fixture, "Abbrechen oder Fehlschlag beim Laden löscht vorgemerktes Alle installieren") {
    focusRow(EmulatorsScene::kRowInstallAll);
    CHECK(press(Action::Confirm).command == AppCommand::EmuFetchList);
    s.setListState(EmulatorsScene::ListState::Loading);
    CHECK(press(Action::Back).command == AppCommand::EmuCancel);
    s.setListState(EmulatorsScene::ListState::Loaded);
    CHECK(s.takePendingTargets().empty());

    s.setListState(EmulatorsScene::ListState::NotLoaded);
    CHECK(press(Action::Confirm).command == AppCommand::EmuFetchList);
    s.setListState(EmulatorsScene::ListState::Failed);
    s.setListState(EmulatorsScene::ListState::Loaded);
    CHECK(s.takePendingTargets().empty());
}

TEST_CASE_FIXTURE(Fixture, "Alle aktualisieren ohne geladene Liste lädt Liste und liefert danach Update-Ziele") {
    focusRow(EmulatorsScene::kRowUpdateAll);
    CHECK(press(Action::Confirm).command == AppCommand::EmuFetchList);
    s.setListState(EmulatorsScene::ListState::Loaded);
    const auto t = s.takePendingTargets();
    REQUIRE(t.size() == 2);
    CHECK(t[0].name == "fceugx");
    CHECK(t[1].name == "snes9xgx");
}

TEST_CASE_FIXTURE(Fixture, "B während Arbeit bricht ab, Szene bleibt; ohne Arbeit schließt B") {
    s.setBusy(EmuProgress{"fceugx", EmuStep::Download, 10, 100, 0, 1});
    SceneRequests r = press(Action::Back);
    CHECK(r.command == AppCommand::EmuCancel);
    CHECK_FALSE(r.pop);
    CHECK(press(Action::Confirm).command == AppCommand::None);  // A während Arbeit gesperrt
    s.draw(ctx, dl);
    s.setBusy(std::nullopt);
    r = press(Action::Back);
    CHECK(r.pop);
    CHECK(r.command == AppCommand::None);
}

TEST_CASE_FIXTURE(Fixture, "B während die Liste lädt bricht ab") {
    s.setListState(EmulatorsScene::ListState::Loading);
    const SceneRequests r = press(Action::Back);
    CHECK(r.command == AppCommand::EmuCancel);
    CHECK_FALSE(r.pop);
}

TEST_CASE_FIXTURE(Fixture, "Zeiger wählt Zeile") {
    InputFrame f;
    f.pointerValid = true;
    f.pointerMoved = true;
    const gfx::Rect r = s.rowRect(ctx, 3);
    f.pointerX = r.x + r.w / 2;
    f.pointerY = r.y + r.h / 2;
    SceneRequests out;
    s.update(ctx, f, 16, out);
    CHECK(s.focus() == 3);
}

TEST_CASE_FIXTURE(Fixture, "Liste fehlgeschlagen: Info nennt die Netzursache (Spec v2 4.4)") {
    s.setListState(EmulatorsScene::ListState::Failed, NetError::Clock);
    CHECK(s.infoText().find(tr(Str::NetErrClock)) != std::string::npos);
    s.setListState(EmulatorsScene::ListState::Failed, NetError::TlsMissing);
    CHECK(s.infoText().find(tr(Str::NetErrTlsMissing)) != std::string::npos);
    s.setListState(EmulatorsScene::ListState::Failed);  // Ursache unbekannt: allgemeiner Text
    CHECK(s.infoText() == tr(Str::EmuListFailed));
    s.setListState(EmulatorsScene::ListState::Loading, NetError::Clock);
    CHECK(s.infoText() == tr(Str::EmuListLoading));
}

TEST_CASE_FIXTURE(Fixture, "Fehler und Statustexte werden gezeichnet") {
    s.setListState(EmulatorsScene::ListState::Failed);
    s.setError("fceugx", EmuInstallError::NoSpace);
    s.draw(ctx, dl);
    CHECK(s.statusText(0) == tr(Str::EmuErrNoSpace));
    CHECK(s.statusText(2) == tr(Str::EmuNotInstalled));
}
