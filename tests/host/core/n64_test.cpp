#include "core/ini.hpp"
#include "core/n64.hpp"
#include <doctest/doctest.h>
#include <string>
using namespace nl;

TEST_CASE("parseN64Method kennt vc/not64/wii64 ohne Rücksicht auf Groß/klein") {
    CHECK(parseN64Method("not64") == N64Method::Not64);
    CHECK(parseN64Method(" WII64 ") == N64Method::Wii64);
    CHECK(parseN64Method("VC") == N64Method::Vc);
    CHECK_FALSE(parseN64Method("n64").has_value());
    CHECK_FALSE(parseN64Method("").has_value());
    CHECK_FALSE(parseN64Method("vc64").has_value());
}

TEST_CASE("n64MethodKey/Label") {
    CHECK(std::string(n64MethodKey(N64Method::Vc)) == "vc");
    CHECK(std::string(n64MethodKey(N64Method::Not64)) == "not64");
    CHECK(std::string(n64MethodKey(N64Method::Wii64)) == "wii64");
    CHECK(std::string(n64MethodLabel(N64Method::Vc)) == "VC-Inject");
    CHECK(std::string(n64MethodLabel(N64Method::Not64)) == "Not64");
    CHECK(std::string(n64MethodLabel(N64Method::Wii64)) == "Wii64");
}

TEST_CASE("GameOverrides liest [n64_method], ignoriert Unbekanntes") {
    const GameOverrides o = GameOverrides::fromIni(IniDoc::parse(
        "[n64_method]\nn64/Super Mario 64 (Europe).z64 = not64\nn64/a.z64 = quatsch\nn64/b.z64 =\n"
        "[other]\nn64/c.z64 = vc\n"));
    CHECK(o.n64Method("n64/Super Mario 64 (Europe).z64") == N64Method::Not64);
    CHECK_FALSE(o.n64Method("n64/a.z64").has_value());
    CHECK_FALSE(o.n64Method("n64/b.z64").has_value());
    CHECK_FALSE(o.n64Method("n64/c.z64").has_value());
    CHECK(o.size() == 1);
}

TEST_CASE("GameOverrides setzen, löschen, serialisieren") {
    GameOverrides o;
    o.setN64Method("n64/k1.z64", N64Method::Wii64);
    o.setN64Method("n64/k2.z64", N64Method::Vc);
    o.setN64Method("n64/k1.z64", std::nullopt);
    const GameOverrides back = GameOverrides::fromIni(IniDoc::parse(o.toIni().serialize()));
    CHECK_FALSE(back.n64Method("n64/k1.z64").has_value());
    CHECK(back.n64Method("n64/k2.z64") == N64Method::Vc);
    CHECK(back.size() == 1);

    o.setN64Method("n64/k2.z64", N64Method::Not64);
    const std::string text = o.toIni().serialize();
    CHECK(text.find("n64/k2.z64") == text.rfind("n64/k2.z64"));
    CHECK(GameOverrides::fromIni(IniDoc::parse(text)).n64Method("n64/k2.z64") == N64Method::Not64);
}

TEST_CASE("GameOverrides: [game ID6] ios lesen, Fehler melden, beim Speichern erhalten (M13)") {
    const GameOverrides o = GameOverrides::fromIni(IniDoc::parse(
        "[n64_method]\nn64/a.z64 = vc\n[game RMCP01]\nios = 222\nvideo = pal60\n[game RSBP01]\nios = 300\n[game RZDP01]\nios = auto\n"));
    CHECK(o.wiiIos("RMCP01") == std::optional<u8>(222));
    CHECK_FALSE(o.wiiIos("RSBP01").has_value());
    CHECK_FALSE(o.wiiIos("RZDP01").has_value());
    CHECK_FALSE(o.wiiIos("RXXP01").has_value());
    CHECK(o.badKeys() == std::vector<std::string>{"game RSBP01.ios"});

    GameOverrides edit = o;
    edit.setN64Method("n64/b.z64", N64Method::Not64);  // N64-Speichern darf [game …] nicht löschen
    const GameOverrides back = GameOverrides::fromIni(edit.toIni());
    CHECK(back.wiiIos("RMCP01") == std::optional<u8>(222));
    CHECK(back.n64Method("n64/a.z64") == N64Method::Vc);
    CHECK(back.n64Method("n64/b.z64") == N64Method::Not64);
    CHECK(edit.toIni().get("game RMCP01", "video") == "pal60");  // unbekannte Schlüssel bleiben
    CHECK(edit.toIni().get("game RSBP01", "ios") == "300");       // auch ungültige Werte schreibt nur der Nutzer um
}

TEST_CASE("GameOverrides: [emulator] <schlüssel> = <profil> (M13)") {
    const GameOverrides o = GameOverrides::fromIni(IniDoc::parse(
        "[emulator]\nnes/Metroid (Test).zip = retroarch-fceumm\nsnes/a.sfc =\n"));
    CHECK(o.emulatorProfile("nes/Metroid (Test).zip") == std::optional<std::string_view>("retroarch-fceumm"));
    CHECK_FALSE(o.emulatorProfile("snes/a.sfc").has_value());
    CHECK_FALSE(o.emulatorProfile("gb/x.gb").has_value());
    GameOverrides edit = o;
    edit.setN64Method("n64/b.z64", N64Method::Wii64);
    CHECK(GameOverrides::fromIni(edit.toIni()).emulatorProfile("nes/Metroid (Test).zip").has_value());
}

TEST_CASE("setN64MethodText ändert nur [n64_method], Kommentare und andere Abschnitte bleiben") {
    const std::string in = "; Meine Overrides\r\n[n64_method]\r\n# Notiz\r\nn64/a.z64 = vc\r\n\r\n[game RMGP01]\r\nvideo = pal60\r\n";
    CHECK(nl::setN64MethodText(in, "n64/a.z64", nl::N64Method::Not64) ==
          "; Meine Overrides\r\n[n64_method]\r\n# Notiz\r\nn64/a.z64 = not64\r\n\r\n[game RMGP01]\r\nvideo = pal60\r\n");
    CHECK(nl::setN64MethodText(in, "n64/a.z64", std::nullopt) ==
          "; Meine Overrides\r\n[n64_method]\r\n# Notiz\r\n\r\n[game RMGP01]\r\nvideo = pal60\r\n");
    CHECK(nl::setN64MethodText("", "n64/b.z64", nl::N64Method::Wii64) == "[n64_method]\nn64/b.z64 = wii64\n");
}
