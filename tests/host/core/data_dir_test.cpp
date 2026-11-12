#include "core/data_dir.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace nl;

TEST_CASE("data_dir: deviceFromArgv0") {
    CHECK(deviceFromArgv0("usb:/apps/newlauncher/boot.dol") == "usb");
    CHECK(deviceFromArgv0("USB2:/apps/x/boot.dol") == "usb2");
    CHECK(deviceFromArgv0("sd:/apps/x/boot.dol") == "sd");
    CHECK(deviceFromArgv0("/apps/x/boot.dol") == "");
    CHECK(deviceFromArgv0("") == "");
    CHECK(deviceFromArgv0("boot.dol") == "");
}

TEST_CASE("data_dir: Stufe 1 – Startgerät mit Ordner") {
    const std::vector<DataCandidate> devs{{"sd", true, true}, {"usb", true, true}};
    const auto c = chooseDataDir("usb:/apps/newlauncher/boot.dol", devs);
    CHECK(c.path == "usb:/newlauncher");
    CHECK_FALSE(c.create);
    CHECK_FALSE(c.readOnly);
}

TEST_CASE("data_dir: Stufe 2 – SD mit Ordner, wenn das Startgerät keinen hat") {
    const std::vector<DataCandidate> devs{{"sd", true, true}, {"usb", true, false}};
    const auto c = chooseDataDir("usb:/apps/newlauncher/boot.dol", devs);
    CHECK(c.path == "sd:/newlauncher");
    CHECK_FALSE(c.create);
}

TEST_CASE("data_dir: Stufe 3 – erstes beschreibbares USB-Gerät mit Ordner") {
    const std::vector<DataCandidate> devs{{"sd", true, false}, {"usb", false, true}, {"usb2", true, true}};
    const auto c = chooseDataDir("sd:/apps/newlauncher/boot.dol", devs);
    CHECK(c.path == "usb2:/newlauncher");
    CHECK_FALSE(c.create);
    CHECK_FALSE(c.readOnly);
}

TEST_CASE("data_dir: Stufe 4 – anlegen auf Startgerät, sonst SD, sonst USB") {
    const std::vector<DataCandidate> devs{{"sd", true, false}, {"usb", true, false}};
    auto c = chooseDataDir("usb:/apps/newlauncher/boot.dol", devs);
    CHECK(c.path == "usb:/newlauncher");
    CHECK(c.create);
    c = chooseDataDir("", devs);
    CHECK(c.path == "sd:/newlauncher");
    CHECK(c.create);
    const std::vector<DataCandidate> noSd{{"sd", false, false}, {"usb", false, false}, {"usb2", true, false}};
    c = chooseDataDir("", noSd);
    CHECK(c.path == "usb2:/newlauncher");
    CHECK(c.create);
}

TEST_CASE("data_dir: Startgerät fehlt oder ist nicht eingehängt → Rückfall") {
    const std::vector<DataCandidate> devs{{"sd", true, true}, {"usb", true, true}};
    CHECK(chooseDataDir("", devs).path == "sd:/newlauncher");
    CHECK(chooseDataDir("/apps/x/boot.dol", devs).path == "sd:/newlauncher");
    CHECK(chooseDataDir("usb9:/apps/x/boot.dol", devs).path == "sd:/newlauncher");
    CHECK(chooseDataDir("USB:/apps/x/boot.dol", devs).path == "usb:/newlauncher");
}

TEST_CASE("data_dir: nichts beschreibbar → nur lesend") {
    const std::vector<DataCandidate> withDir{{"sd", false, false}, {"usb", false, true}};
    auto c = chooseDataDir("", withDir);
    CHECK(c.path == "usb:/newlauncher");
    CHECK(c.readOnly);
    CHECK_FALSE(c.create);
    const std::vector<DataCandidate> none{{"sd", false, false}};
    c = chooseDataDir("", none);
    CHECK(c.path.empty());
    CHECK(c.readOnly);
    CHECK(chooseDataDir("", {}).path.empty());
}
