#include "core/emulators.hpp"
#include "core/ini.hpp"
#include <doctest/doctest.h>
using namespace nl;

TEST_CASE("Standardwerte wie Spec 5.4") {
    const EmulatorTable t = EmulatorTable::fromIni(IniDoc::parse(""));
    REQUIRE(t.forConsole(Console::Gba) != nullptr);
    CHECK(t.forConsole(Console::Gba)->dol == "sd:/apps/vbagx/boot.dol");
    CHECK(t.forConsole(Console::Gba)->args == "{dir}|{file}");
    CHECK(t.forConsole(Console::Nes)->dol == "sd:/apps/fceugx/boot.dol");
    CHECK(t.forConsole(Console::Snes)->dol == "sd:/apps/snes9xgx/boot.dol");
    CHECK(t.forConsole(Console::Gb)->dol == "sd:/apps/vbagx/boot.dol");
    CHECK(t.forConsole(Console::Gbc)->dol == "sd:/apps/vbagx/boot.dol");
}

TEST_CASE("Datei überschreibt nur vorhandene Schlüssel") {
    const EmulatorTable t = EmulatorTable::fromIni(IniDoc::parse("[nes]\ndol = sd:/apps/argv_echo/boot.dol\n"));
    CHECK(t.forConsole(Console::Nes)->dol == "sd:/apps/argv_echo/boot.dol");
    CHECK(t.forConsole(Console::Nes)->args == "{dir}|{file}");
    CHECK(t.forConsole(Console::Snes)->dol == "sd:/apps/snes9xgx/boot.dol");
}

TEST_CASE("leeres dol fällt auf den Standard zurück, leeres args bleibt leer") {
    const EmulatorTable t = EmulatorTable::fromIni(IniDoc::parse("[snes]\ndol =\nargs =\n"));
    CHECK(t.forConsole(Console::Snes)->dol == "sd:/apps/snes9xgx/boot.dol");
    CHECK(t.forConsole(Console::Snes)->args.empty());
}

TEST_CASE("N64, GC und Wii liefern nullptr") {
    const EmulatorTable t = EmulatorTable::fromIni(IniDoc::parse(""));
    CHECK(t.forConsole(Console::N64) == nullptr);
    CHECK(t.forConsole(Console::Gc) == nullptr);
    CHECK(t.forConsole(Console::Wii) == nullptr);
}

TEST_CASE("defaultEmulatorsText ist nur ein Kommentarkopf (M13: Erkennung statt v1-Pfade)") {
    const std::string text = defaultEmulatorsText();
    const IniDoc doc = IniDoc::parse(text);
    CHECK(doc.sectionNames().empty());
    const EmulatorTable fromText = EmulatorTable::fromIni(doc);
    for (Console c : {Console::Nes, Console::Snes, Console::Gb, Console::Gbc, Console::Gba}) {
        CHECK(fromText.forConsole(c)->dol == std::string(v1DefaultDol(consoleInfo(c).key)));
        CHECK_FALSE(fromText.forConsole(c)->fromFile);
    }
    CHECK(v1DefaultDol("nintendont") == "sd:/apps/nintendont/boot.dol");
    CHECK(v1DefaultDol("n64") .empty());
}

TEST_CASE("fromFile, section und set") {
    const EmulatorTable t = EmulatorTable::fromIni(IniDoc::parse("[gba]\ndol = usb:/apps/x/boot.dol\n"));
    CHECK(t.section("gba")->fromFile);
    CHECK_FALSE(t.section("gb")->fromFile);
    CHECK(t.section("n64_wii64") == t.forN64(N64Method::Wii64));
    CHECK(t.section("wii") == nullptr);
    EmulatorTable u = t;
    u.set("nintendont", EmulatorEntry{"usb:/apps/nintendont/boot.dol", "x", false, "nintendont", 0});
    CHECK(u.nintendont().dol == "usb:/apps/nintendont/boot.dol");
    CHECK(u.nintendont().args.empty());
    u.set("snes", EmulatorEntry{"sd:/apps/retroarch-wii/snes9x2010_libretro_wii.dol", "{dir}|{file}", false, "retroarch-snes9x2010", 0});
    CHECK(u.forConsole(Console::Snes)->profile == "retroarch-snes9x2010");
}

TEST_CASE("forN64 liefert Not64/Wii64 aus emulators.ini, VC nullptr") {
    const EmulatorTable d = EmulatorTable::fromIni(IniDoc::parse(""));
    REQUIRE(d.forN64(N64Method::Not64) != nullptr);
    CHECK(d.forN64(N64Method::Not64)->dol == "sd:/apps/not64/boot.dol");
    CHECK(d.forN64(N64Method::Not64)->args == "rompath=\"{path}\"");  // Not64 liest argv als Schlüssel=Wert
    REQUIRE(d.forN64(N64Method::Wii64) != nullptr);
    CHECK(d.forN64(N64Method::Wii64)->dol == "sd:/apps/wii64/boot.dol");
    CHECK(d.forN64(N64Method::Wii64)->args == "{path}");
    CHECK(d.forN64(N64Method::Vc) == nullptr);
    CHECK(d.forConsole(Console::N64) == nullptr);

    const EmulatorTable t = EmulatorTable::fromIni(IniDoc::parse("[n64_not64]\ndol = sd:/apps/argv_echo/boot.dol\n[n64_wii64]\ndol =\n"));
    CHECK(t.forN64(N64Method::Not64)->dol == "sd:/apps/argv_echo/boot.dol");
    CHECK(t.forN64(N64Method::Not64)->args == "rompath=\"{path}\"");
    CHECK(t.forN64(N64Method::Wii64)->dol == "sd:/apps/wii64/boot.dol");
}
