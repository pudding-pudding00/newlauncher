// M27: Cloud-Menü, Gerätecode-Szene, Abgleich-Szene und Konflikttext.
#include "ui/cloud_scene.hpp"
#include "ui/cloud_sync_scene.hpp"
#include "ui/device_code_scene.hpp"
#include "ui/pairing_scene.hpp"
#include "ui/qr_view.hpp"
#include "core/qr.hpp"
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

TEST_CASE_FIXTURE(Fixture, "CloudScene M28: Anbieter-Zyklus Aus → Drive → OneDrive, ohne Client-ID übersprungen") {
    CloudScene s;
    CloudView v = view(CloudProviderId::None, CloudView::Status::NotLinked);
    s.open(v);
    step(s, action(Action::Right));
    CHECK(s.provider() == CloudProviderId::Drive);
    step(s, action(Action::Right));
    CHECK(s.provider() == CloudProviderId::OneDrive);
    step(s, action(Action::Right));
    CHECK(s.provider() == CloudProviderId::None);
    step(s, action(Action::Left));
    CHECK(s.provider() == CloudProviderId::OneDrive);
    s.draw(ctx, dl);

    v.onedriveReady = false;
    s.open(v);
    step(s, action(Action::Right));
    CHECK(s.provider() == CloudProviderId::Drive);
    step(s, action(Action::Right));
    CHECK(s.provider() == CloudProviderId::None);
    step(s, action(Action::Left));
    CHECK(s.provider() == CloudProviderId::Drive);

    v.driveReady = false;
    s.open(v);
    CHECK(step(s, action(Action::Right)).command == AppCommand::None);  // nichts wählbar
    CHECK(s.provider() == CloudProviderId::None);
}

TEST_CASE_FIXTURE(Fixture, "CloudScene M28: OneDrive angemeldet → Anmelden erlaubt") {
    CloudScene s;
    s.open(view(CloudProviderId::OneDrive, CloudView::Status::NotLinked));
    CHECK(s.rowEnabled(CloudScene::kRowLink));
}

TEST_CASE_FIXTURE(Fixture, "CloudScene M28: Alle abgleichen nur angemeldet, A → CloudSyncAll, beim Abgleich Abbruch") {
    CloudScene s;
    CloudView v = view(CloudProviderId::Drive, CloudView::Status::NotLinked);
    s.open(v);
    CHECK_FALSE(s.rowEnabled(CloudScene::kRowSyncAll));
    v.status = CloudView::Status::Relink;
    s.setView(v);
    CHECK_FALSE(s.rowEnabled(CloudScene::kRowSyncAll));
    v.status = CloudView::Status::Linked;
    s.setView(v);
    CHECK(s.rowEnabled(CloudScene::kRowSyncAll));
    step(s, action(Action::Down));
    step(s, action(Action::Down));
    CHECK(s.focus() == CloudScene::kRowSyncAll);
    CHECK(step(s, action(Action::Confirm)).command == AppCommand::CloudSyncAll);
    CHECK(std::string(appCommandName(AppCommand::CloudSyncAll)) == "cloud_sync_all");

    v.syncing = true;
    s.setView(v);
    CHECK(s.rowEnabled(CloudScene::kRowSyncAll));
    CHECK_FALSE(s.rowEnabled(CloudScene::kRowProvider));
    CHECK_FALSE(s.rowEnabled(CloudScene::kRowUnlink));
    CHECK(s.focus() == CloudScene::kRowSyncAll);
    CHECK(step(s, action(Action::Confirm)).command == AppCommand::CloudSyncAllCancel);
    CHECK(std::string(appCommandName(AppCommand::CloudSyncAllCancel)) == "cloud_sync_all_cancel");
    s.draw(ctx, dl);
}

TEST_CASE("M28: neue Cloud-Strings in beiden Sprachen") {
    const Str ids[] = {Str::CloudProviderOneDrive, Str::CloudSyncAll,   Str::CloudSyncAllCancel,
                       Str::CloudSyncProgress,     Str::CloudSyncDone,  Str::CloudSwitchTitle,
                       Str::CloudSwitchWii,        Str::CloudSwitchCloud, Str::CloudSwitchAsk,
                       Str::CloudUnlinkedRevokeFail};
    for (Lang l : {Lang::De, Lang::En}) {
        setLanguage(l);
        for (Str id : ids) CHECK(std::string(tr(id)).size() > 0);
        CHECK(std::string(tr(Str::CloudSyncProgress)).find("%u/%u") != std::string::npos);
    }
    setLanguage(Lang::De);
    CHECK(std::string(tr(Str::CloudUnlinkedRevokeFail)) == "Abgemeldet, Widerruf nicht bestätigt");
    setLanguage(Lang::En);
    CHECK(std::string(tr(Str::CloudUnlinkedRevokeFail)) == "Signed out, revocation not confirmed");
    setLanguage(Lang::De);
}

TEST_CASE_FIXTURE(Fixture, "PairingScene M29: QR der Kopplungsseite, Y wechselt auf den direkten QR") {
    PairingScene s;
    s.open();
    s.setLink("http://192.0.2.10:4301/0123456789abcdef0123456789abcdef",
              "https://www.dropbox.com/oauth2/authorize?client_id=k&response_type=code", 300);
    CHECK_FALSE(s.directQr());
    CHECK(s.qrText() == "http://192.0.2.10:4301/0123456789abcdef0123456789abcdef");
    s.draw(ctx, dl);
    CHECK(dl.droppedQuads() == 0);
    const SceneRequests y = step(s, action(Action::Favorite));
    CHECK(s.directQr());
    CHECK(y.sound == UiSound::Move);
    CHECK(s.qrText().rfind("https://www.dropbox.com/oauth2/authorize", 0) == 0);
    dl.clear();
    s.draw(ctx, dl);
    CHECK(dl.droppedQuads() == 0);
    step(s, action(Action::Favorite));
    CHECK_FALSE(s.directQr());
}

TEST_CASE_FIXTURE(Fixture, "PairingScene M29: ohne Kopplungsadresse gleich der direkte QR, Y bleibt") {
    PairingScene s;
    s.open();
    s.setLink("", "https://www.dropbox.com/oauth2/authorize?client_id=k", 300);
    CHECK(s.directQr());
    step(s, action(Action::Favorite));
    CHECK(s.directQr());
    s.draw(ctx, dl);
}

TEST_CASE_FIXTURE(Fixture, "PairingScene M29: A → CloudEnterCode ohne Schließen, B → CloudCancelLink") {
    PairingScene s;
    s.open();
    s.setLink("http://10.0.0.2:4301/t", "https://www.dropbox.com/oauth2/authorize", 300);
    const SceneRequests a = step(s, action(Action::Confirm));
    CHECK(a.command == AppCommand::CloudEnterCode);
    CHECK_FALSE(a.pop);
    CHECK(std::string(appCommandName(AppCommand::CloudEnterCode)) == "cloud_enter_code");
    const SceneRequests b = step(s, action(Action::Back));
    CHECK(b.pop);
    CHECK(b.command == AppCommand::CloudCancelLink);
}

TEST_CASE_FIXTURE(Fixture, "PairingScene M29: Restzeit zählt herunter, requestClose schließt ohne Befehl") {
    PairingScene s;
    s.open();
    s.setLink("http://10.0.0.2:4301/t", "https://www.dropbox.com/oauth2/authorize", 300);
    CHECK(s.remainingText() == "05:00");
    step(s, InputFrame{}, 1000.0f);
    CHECK(s.remainingText() == "04:59");
    for (int i = 0; i < 400; ++i) step(s, InputFrame{}, 1000.0f);
    CHECK(s.remainingText() == "00:00");
    s.setStatus("Code abgelehnt");
    s.draw(ctx, dl);
    s.requestClose();
    const SceneRequests r = step(s, InputFrame{});
    CHECK(r.pop);
    CHECK(r.command == AppCommand::None);
}

TEST_CASE_FIXTURE(Fixture, "drawQr M29: weißer Rand, Module im Bereich, Zeilenläufe zusammengefasst") {
    const auto q = encodeQr(std::string(213, 'x'));  // Version 10
    REQUIRE(q.has_value());
    const gfx::Rect area{100, 50, 300, 300};
    dl.clear();
    drawQr(dl, kShapes, *q, area);
    CHECK(dl.droppedQuads() == 0);
    CHECK(dl.quadCount() < 1200u);
    for (const auto& v : dl.vertices()) {
        CHECK(v.x >= area.x);
        CHECK(v.x <= area.x + area.w);
        CHECK(v.y >= area.y);
        CHECK(v.y <= area.y + area.h);
    }
}

TEST_CASE("M29: Kopplungs-Strings in beiden Sprachen") {
    const Str ids[] = {Str::CloudProviderDropbox, Str::PairTitle, Str::PairScanPhone, Str::PairScanDirect,
                       Str::PairHelp,             Str::PairCodeHint, Str::PairCodeRejected, Str::PairWaiting};
    for (Lang l : {Lang::De, Lang::En}) {
        setLanguage(l);
        for (Str id : ids) CHECK(std::string(tr(id)).size() > 0);
    }
    setLanguage(Lang::En);
    CHECK(std::string(tr(Str::PairTitle)) == "Connect Dropbox");
    setLanguage(Lang::De);
    CHECK(std::string(tr(Str::PairTitle)) == "Dropbox verbinden");
}
