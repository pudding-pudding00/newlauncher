#include <doctest/doctest.h>

#include "core/config.hpp"
#include "core/ini.hpp"
#include "core/settings_model.hpp"
#include "ui/strings.hpp"
#include "ui/wiiload_scene.hpp"

using namespace nl;
using namespace nl::ui;

namespace {

InputFrame press(Action a) {
    InputFrame in{};
    in.actions = 1u << static_cast<u32>(a);
    return in;
}

}  // namespace

TEST_CASE("config parses [network] wiiload and [debug] wiiload/wiiload_port") {
    {
        auto c = configFromIni(IniDoc::parse(""));
        CHECK(c.wiiloadMode == WiiloadMode::Off);
        CHECK(c.badKeys.empty());
    }
    {
        auto c = configFromIni(IniDoc::parse("[network]\nwiiload = confirm\n[debug]\nwiiload = auto\nwiiload_port = 4399\n"));
        CHECK(c.wiiloadMode == WiiloadMode::Confirm);
        CHECK(c.debugWiiload == "auto");
        CHECK(c.debugWiiloadPort == 4399);
        CHECK(c.badKeys.empty());
    }
    {
        auto c = configFromIni(IniDoc::parse("[network]\nwiiload = bogus\n"));
        CHECK(c.wiiloadMode == WiiloadMode::Off);
        REQUIRE(c.badKeys.size() == 1);
        CHECK(c.badKeys[0] == "network.wiiload");
    }
    {
        Config c;
        c.wiiloadMode = WiiloadMode::Auto;
        stepSetting(c, SettingRow::Wiiload, 1);
        CHECK(c.wiiloadMode == WiiloadMode::Off);
        stepSetting(c, SettingRow::Wiiload, 1);
        CHECK(c.wiiloadMode == WiiloadMode::Confirm);
    }
}

TEST_CASE("WiiloadScene emits Accept, TrustSender, and Decline commands") {
    WiiloadScene scene;
    WiiloadSummary sum;
    sum.senderIp = "192.168.1.42";
    sum.kind = WiiloadKind::Zip;
    sum.fileName = "demo.zip";
    sum.sizeBytes = 65536;
    sum.targetApp = "demo";
    sum.args = {"demo.zip", "--fast"};
    scene.open(sum);

    UiContext ctx;
    {
        SceneRequests req;
        scene.update(ctx, press(Action::Confirm), 16.0f, req);
        CHECK(req.pop);
        CHECK(req.command == AppCommand::WiiloadAccept);
    }
    {
        scene.open(sum);
        SceneRequests r1;
        scene.update(ctx, press(Action::Right), 16.0f, r1);
        CHECK(scene.focused() == 1);

        SceneRequests r2;
        scene.update(ctx, press(Action::Confirm), 16.0f, r2);
        CHECK(r2.pop);
        CHECK(r2.command == AppCommand::WiiloadTrustSender);
    }
    {
        scene.open(sum);
        SceneRequests r;
        scene.update(ctx, press(Action::Back), 16.0f, r);
        CHECK(r.pop);
        CHECK(r.command == AppCommand::WiiloadDecline);
    }

    setLanguage(Lang::De);
    CHECK(std::string_view(tr(Str::WiiloadTitle)) != "");
    setLanguage(Lang::En);
    CHECK(std::string_view(tr(Str::WiiloadTitle)) != "");
}
