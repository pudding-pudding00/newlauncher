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

TEST_CASE("N64, GC und Wii liefern nullptr") {
    const EmulatorTable t = EmulatorTable::fromIni(IniDoc::parse(""));
    CHECK(t.forConsole(Console::N64) == nullptr);
    CHECK(t.forConsole(Console::Gc) == nullptr);
    CHECK(t.forConsole(Console::Wii) == nullptr);
}

TEST_CASE("defaultEmulatorsText ergibt nach dem Parsen dieselben Werte und enthält [n64_not64], [n64_wii64], [nintendont]") {
    const std::string text = defaultEmulatorsText();
    const IniDoc doc = IniDoc::parse(text);
    const EmulatorTable fromText = EmulatorTable::fromIni(doc);
    const EmulatorTable defaults = EmulatorTable::fromIni(IniDoc::parse(""));
    for (Console c : {Console::Nes, Console::Snes, Console::Gb, Console::Gbc, Console::Gba}) {
        CHECK(fromText.forConsole(c)->dol == defaults.forConsole(c)->dol);
        CHECK(fromText.forConsole(c)->args == defaults.forConsole(c)->args);
    }
    CHECK(text.find("[n64_not64]") != std::string::npos);
    CHECK(text.find("[n64_wii64]") != std::string::npos);
    CHECK(text.find("[nintendont]") != std::string::npos);
    CHECK(doc.get("n64_not64", "args") == "{path}");
    CHECK(doc.get("nintendont", "dol") == "sd:/apps/nintendont/boot.dol");
}
