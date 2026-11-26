#include "ui/wifi_scene.hpp"
#include "ui/strings.hpp"
#include "support/fake_font.hpp"
#include <doctest/doctest.h>
#include <string>
#include <string_view>
using namespace nl;
using namespace nl::ui;

namespace {
const gfx::AtlasShapes kShapes{gfx::UvRect{0, 0, 0.01f, 0.01f}, gfx::UvRect{0.5f, 0.5f, 0.6f, 0.6f}};

struct LangGuard {
    Lang saved = language();
    ~LangGuard() { setLanguage(saved); }
};

WifiSnapshot connected() {
    WifiSnapshot s;
    s.status = WifiStatus::Connected;
    s.ip = 0xC000020Au;
    s.netmask = 0xFFFFFF00u;
    s.gateway = 0xC0A80101u;
    s.mac = {0x00, 0x1f, 0x32, 0xab, 0xcd, 0xef};
    s.macKnown = true;
    s.gatewayMs = 7;
    s.checked = true;
    s.lastCheckMs = 10000;
    s.connectedSinceMs = 5000;
    return s;
}

struct Fixture {
    gfx::FontSet fonts = test::fakeFontSet(8, 16);
    UiContext ctx;
    gfx::DrawList dl{16384, 16};
    WifiScene s;
    LangGuard guard;
    Fixture() {
        ctx.theme = &lightTheme();
        ctx.fonts = &fonts;
        ctx.shapes = &kShapes;
        setLanguage(Lang::De);
        s.setSnapshot(connected(), 12000);
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

bool anyLineContains(const WifiScene& s, std::string_view needle) {
    for (const std::string& l : s.lines())
        if (l.find(needle) != std::string::npos) return true;
    return false;
}
}  // namespace

TEST_CASE_FIXTURE(Fixture, "Verbunden: Adressen, Router-Antwort, Dauer, keine Tipps") {
    CHECK(anyLineContains(s, "Verbunden"));
    CHECK(anyLineContains(s, "192.0.2.10"));
    CHECK(anyLineContains(s, "255.255.255.0"));
    CHECK(anyLineContains(s, "192.168.1.1"));
    CHECK(anyLineContains(s, "7 ms"));
    CHECK(anyLineContains(s, "00:1F:32:AB:CD:EF"));
    CHECK(anyLineContains(s, "0:07"));       // verbunden seit 7 s
    CHECK(anyLineContains(s, "vor 2 s"));    // letzte Prüfung
    CHECK_FALSE(anyLineContains(s, "WPA2"));
    CHECK_FALSE(anyLineContains(s, "Fehlercode"));
}

TEST_CASE_FIXTURE(Fixture, "Getrennt: Fehlercode, Fehlschläge und Tipps") {
    WifiSnapshot d;
    d.status = WifiStatus::Disconnected;
    d.lastInitCode = -116;
    d.failedProbes = 3;
    d.checked = true;
    s.setSnapshot(d, 1000);
    CHECK(anyLineContains(s, "Nicht verbunden"));
    CHECK(anyLineContains(s, "-116"));
    CHECK(anyLineContains(s, "3"));
    CHECK(anyLineContains(s, "WPA2"));
    CHECK(anyLineContains(s, "2,4 GHz"));
    CHECK_FALSE(anyLineContains(s, "Verbunden seit"));
}

TEST_CASE_FIXTURE(Fixture, "Eingeschränkt: Router antwortet nicht, Tipps sichtbar") {
    WifiSnapshot l = connected();
    l.status = WifiStatus::Limited;
    l.gatewayMs = -1;
    l.gatewayFails = 2;
    s.setSnapshot(l, 12000);
    CHECK(anyLineContains(s, "Eingeschränkt"));
    CHECK(anyLineContains(s, "keine"));
    CHECK(anyLineContains(s, "WPA2"));
}

TEST_CASE_FIXTURE(Fixture, "Englisch") {
    setLanguage(Lang::En);
    s.setSnapshot(connected(), 12000);
    CHECK(anyLineContains(s, "Connected"));
    CHECK(anyLineContains(s, "IP address"));
    CHECK(anyLineContains(s, "2 s ago"));
}

TEST_CASE_FIXTURE(Fixture, "HTTPS-Zeile nur im TLS-Build") {
    CHECK_FALSE(anyLineContains(s, "HTTPS"));
    WifiSnapshot t = connected();
    t.tlsBuild = true;
    t.tlsUp = true;
    s.setSnapshot(t, 12000);
    CHECK(anyLineContains(s, "HTTPS"));
}

TEST_CASE_FIXTURE(Fixture, "A verbindet neu, B schließt; während Connecting kein zweites A") {
    CHECK(press(Action::Confirm).command == AppCommand::WifiReconnect);
    WifiSnapshot c;
    c.status = WifiStatus::Connecting;
    s.setSnapshot(c, 0);
    CHECK(press(Action::Confirm).command == AppCommand::None);
    CHECK(press(Action::Back).pop);
    CHECK(press(Action::Home).pop);
    CHECK(std::string(appCommandName(AppCommand::WifiReconnect)) == "wifi_reconnect");
}

TEST_CASE_FIXTURE(Fixture, "Blättern bleibt in den Grenzen; Zeichnen ohne Absturz") {
    WifiSnapshot d;
    d.status = WifiStatus::Disconnected;
    d.lastInitCode = -6;
    d.checked = true;
    s.setSnapshot(d, 0);
    for (int i = 0; i < 50; ++i) press(Action::Down);
    CHECK(s.scroll() == s.maxScroll(ctx));
    for (int i = 0; i < 50; ++i) press(Action::Up);
    CHECK(s.scroll() == 0);
    s.draw(ctx, dl);
    CHECK(dl.quadCount() > 0);
}
