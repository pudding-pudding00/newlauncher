// M27: Cloud-Menü, Gerätecode-Szene, Abgleich-Szene und Konflikttext.
#include "ui/cloud_scene.hpp"
#include "ui/cloud_sync_scene.hpp"
#include "ui/device_code_scene.hpp"
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
    gfx::DrawList dl{16384, 16};
    Fixture() {
        ctx.theme = &lightTheme();
        ctx.fonts = &fonts;
        ctx.shapes = &kShapes;
        ctx.width = 640;
        ctx.height = 480;
        setLanguage(Lang::De);
    }
    ~Fixture() { setLanguage(Lang::De); }
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
};

CloudView view(CloudProviderId p, CloudView::Status st) {
    CloudView v;
    v.provider = p;
    v.status = st;
    return v;
}
}  // namespace

TEST_CASE_FIXTURE(Fixture, "CloudScene: Anbieter umschalten meldet CloudSetProvider") {
    CloudScene s;
    s.open(view(CloudProviderId::None, CloudView::Status::NotLinked));
    CHECK(s.isOverlay());
    CHECK(s.focus() == CloudScene::kRowProvider);
    const SceneRequests r = step(s, action(Action::Right));
    CHECK(r.command == AppCommand::CloudSetProvider);
    CHECK(s.provider() == CloudProviderId::Drive);
    CHECK(step(s, action(Action::Left)).command == AppCommand::CloudSetProvider);
    CHECK(s.provider() == CloudProviderId::None);
    CHECK(std::string(appCommandName(AppCommand::CloudSetProvider)) == "cloud_set_provider");
}

TEST_CASE_FIXTURE(Fixture, "CloudScene: Anmelden mit Client-ID meldet CloudLink") {
    CloudScene s;
    s.open(view(CloudProviderId::Drive, CloudView::Status::NotLinked));
    CHECK(s.rowEnabled(CloudScene::kRowLink));
    CHECK_FALSE(s.rowEnabled(CloudScene::kRowUnlink));
    step(s, action(Action::Down));
    CHECK(s.focus() == CloudScene::kRowLink);
    CHECK(step(s, action(Action::Confirm)).command == AppCommand::CloudLink);
}

TEST_CASE_FIXTURE(Fixture, "CloudScene: ohne Client-ID ist Anmelden ausgegraut") {
    CloudScene s;
    s.open(view(CloudProviderId::Drive, CloudView::Status::NoClientId));
    CHECK_FALSE(s.rowEnabled(CloudScene::kRowLink));
    CHECK(std::string(s.statusText()).find("Client-ID") != std::string::npos);
    step(s, action(Action::Down));
    CHECK(s.focus() == CloudScene::kRowProvider);  // gesperrte Zeilen überspringt der Fokus
    CHECK(step(s, action(Action::Confirm)).command != AppCommand::CloudLink);
}

TEST_CASE_FIXTURE(Fixture, "CloudScene: ohne TLS ist alles gesperrt") {
    CloudScene s;
    s.open(view(CloudProviderId::Drive, CloudView::Status::NoTls));
    CHECK_FALSE(s.rowEnabled(CloudScene::kRowProvider));
    CHECK_FALSE(s.rowEnabled(CloudScene::kRowLink));
    CHECK(step(s, action(Action::Right)).command == AppCommand::None);
    CHECK(step(s, action(Action::Back)).pop);
}

TEST_CASE_FIXTURE(Fixture, "CloudScene: angemeldet → Abmelden meldet CloudUnlink, solange nichts läuft") {
    CloudScene s;
    CloudView v = view(CloudProviderId::Drive, CloudView::Status::Linked);
    s.open(v);
    CHECK(s.rowEnabled(CloudScene::kRowUnlink));
    step(s, action(Action::Down));
    step(s, action(Action::Down));
    CHECK(s.focus() == CloudScene::kRowUnlink);
    CHECK(step(s, action(Action::Confirm)).command == AppCommand::CloudUnlink);
    v.busy = true;
    s.setView(v);
    CHECK_FALSE(s.rowEnabled(CloudScene::kRowUnlink));
    CHECK(step(s, action(Action::Confirm)).command == AppCommand::None);
    s.draw(ctx, dl);
}

TEST_CASE_FIXTURE(Fixture, "DeviceCodeScene: Restzeit mm:ss, B bricht ab") {
    DeviceCodeScene s;
    s.open();
    CHECK(s.waiting());
    s.draw(ctx, dl);
    s.setCode("https://www.google.com/device", "ABCD-EFGH", 1800);
    CHECK_FALSE(s.waiting());
    CHECK(s.remainingText() == "30:00");
    step(s, InputFrame{}, 1000.0f);
    CHECK(s.remainingText() == "29:59");
    for (int i = 0; i < 2000; ++i) step(s, InputFrame{}, 1000.0f);
    CHECK(s.remainingText() == "00:00");
    s.draw(ctx, dl);
    const SceneRequests r = step(s, action(Action::Back));
    CHECK(r.pop);
    CHECK(r.command == AppCommand::CloudCancelLink);
}

TEST_CASE_FIXTURE(Fixture, "CloudSyncScene: B → CloudSkip und schließen") {
    CloudSyncScene s;
    s.open("Super Mario Galaxy");
    CHECK(s.isOverlay());
    CHECK(s.phase() == CloudPhase::Checking);
    s.draw(ctx, dl);
    s.setPhase(CloudPhase::Resolving);
    CHECK(step(s, InputFrame{}).command == AppCommand::None);
    const SceneRequests r = step(s, action(Action::Back));
    CHECK(r.command == AppCommand::CloudSkip);
    CHECK(r.pop);
}

TEST_CASE_FIXTURE(Fixture, "Konflikttext: Wii-Zeit, Cloud-Zeit und Gerät, DE und EN") {
    // 1800000000 = 2027-01-15 08:00:00 UTC, 1700000000 = 2023-11-14 22:13:20 UTC
    CHECK(conflictText(1800000000, 1700000000, "Wohnzimmer") ==
          "Wii: 15.01.2027 08:00 · Cloud: 14.11.2023 22:13, Wohnzimmer");
    CHECK(conflictText(0, 1700000000, "") == "Wii: unbekannt · Cloud: 14.11.2023 22:13");
    setLanguage(Lang::En);
    CHECK(conflictText(1800000000, 1700000000, "Dolphin") ==
          "Wii: 2027-01-15 08:00 · Cloud: 2023-11-14 22:13, Dolphin");
    CHECK(std::string(tr(Str::CloudKeepWii)) == "Keep Wii save");
}

TEST_CASE_FIXTURE(Fixture, "requestClose schließt Abgleich- und Code-Szene ohne Befehl (M27)") {
    CloudSyncScene sync;
    sync.open("Zelda");
    sync.requestClose();
    SceneRequests r = step(sync, InputFrame{});
    CHECK(r.pop);
    CHECK(r.command == AppCommand::None);
    CHECK(step(sync, InputFrame{}).pop);  // bleibt stehen: unter einem Dialog zählt die Anforderung erst als oberste
    sync.requestClose();
    sync.open("Zelda");  // open setzt die Anforderung zurück
    CHECK_FALSE(step(sync, InputFrame{}).pop);

    DeviceCodeScene code;
    code.open();
    code.requestClose();
    r = step(code, InputFrame{});
    CHECK(r.pop);
    CHECK(r.command == AppCommand::None);
}
