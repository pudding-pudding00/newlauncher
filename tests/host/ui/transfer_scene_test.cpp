#include <doctest/doctest.h>

#include "core/config.hpp"
#include "core/ini.hpp"
#include "core/settings_model.hpp"
#include "ui/strings.hpp"
#include "ui/transfer_scene.hpp"

using namespace nl;
using namespace nl::ui;

TEST_CASE("config parses [network] ftp_port, ftp_user and [debug] ftp_port/ftp_pass") {
    {
        auto c = configFromIni(IniDoc::parse(""));
        CHECK(c.ftpPort == 21);
        CHECK(c.ftpUser == "wii");
        CHECK(c.badKeys.empty());
    }
    {
        auto c = configFromIni(IniDoc::parse("[network]\nftp_port = 2121\nftp_user = admin\n[debug]\nftp_port = 2222\nftp_pass = 246810\n"));
        CHECK(c.ftpPort == 2121);
        CHECK(c.ftpUser == "admin");
        CHECK(c.debugFtpPort == 2222);
        CHECK(c.debugFtpPass == "246810");
        CHECK(c.badKeys.empty());
    }
    {
        auto c = configFromIni(IniDoc::parse("[network]\nftp_port = 99999\n"));
        CHECK(c.ftpPort == 21);
        REQUIRE(c.badKeys.size() == 1);
        CHECK(c.badKeys[0] == "network.ftp_port");
    }
    CHECK(isAction(SettingRow::Transfer));
}

TEST_CASE("TransferScene emits CloseTransfer on Back and has DE/EN strings") {
    TransferScene scene;
    TransferView v;
    v.listening = true;
    v.uri = "ftp://wii:123456@192.168.1.50/";
    v.user = "wii";
    v.pin = "123456";
    v.mounts = {{"sd", true}, {"usb2", false}};
    scene.open(v);

    UiContext ctx;
    InputFrame back{};
    back.actions = 1u << static_cast<u32>(Action::Back);
    SceneRequests req;
    scene.update(ctx, back, 16.0f, req);
    CHECK(req.pop);
    CHECK(req.command == AppCommand::CloseTransfer);

    setLanguage(Lang::De);
    CHECK(std::string_view(tr(Str::TransferTitle)) != "");
    setLanguage(Lang::En);
    CHECK(std::string_view(tr(Str::TransferTitle)) != "");
}
