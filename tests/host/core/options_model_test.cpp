#include "core/options_model.hpp"
#include <doctest/doctest.h>
#include <string>
using namespace nl;

namespace {
std::size_t rowOf(const OptionsModel& m, std::string_view key) {
    for (std::size_t r = 0; r < m.rows(); ++r)
        if (m.key(r) == key) return r;
    FAIL("Zeile fehlt: " << std::string(key));
    return 0;
}
}  // namespace

TEST_CASE("OptionsModel: Wii 15 Zeilen, GC 11, andere Konsolen keine") {
    CHECK(OptionsModel(Console::Wii, "RMGP01", "", "").rows() == 15);
    CHECK(OptionsModel(Console::Gc, "GM8P01", "", "").rows() == 11);
    CHECK(OptionsModel(Console::Nes, "", "", "").rows() == 0);
}

TEST_CASE("OptionsModel: ohne Eintrag im Spiel geerbt, mit globalem Wert als Vorgabe") {
    const OptionsModel m(Console::Wii, "RMGP01", "[wii]\nvideo = pal60\n", "");
    const std::size_t r = rowOf(m, "video");
    CHECK(m.inherited(r));
    CHECK(m.shownValue(r) == "pal60");
    CHECK(m.choice(r) == 0);  // „default“ an Position 0
    CHECK(m.choices(r).front() == "default");
}

TEST_CASE("OptionsModel: globale Szene erbt nie und kennt kein default") {
    const OptionsModel m(Console::Wii, "", "[wii]\nvideo = pal60\n", "");
    const std::size_t r = rowOf(m, "video");
    CHECK_FALSE(m.inherited(r));
    CHECK(m.shownValue(r) == "pal60");
    CHECK(m.choices(r).front() == "game");
}

TEST_CASE("OptionsModel: step schreibt den Spielabschnitt, links von default läuft auf den letzten Wert") {
    OptionsModel m(Console::Wii, "RMGP01", "", "[n64_method]\nn64/a.z64 = vc\n");
    const std::size_t r = rowOf(m, "video");
    CHECK_FALSE(m.dirty());
    m.step(r, -1);
    CHECK(m.shownValue(r) == "progressive");
    CHECK_FALSE(m.inherited(r));
    CHECK(m.dirty());
    CHECK(m.gamesIniText() == "[n64_method]\nn64/a.z64 = vc\n\n[game RMGP01]\nvideo = progressive\n");
    m.step(r, +1);  // zurück auf default: Schlüssel entfällt
    CHECK(m.inherited(r));
    CHECK(m.gamesIniText().find("video") == std::string::npos);
}

TEST_CASE("OptionsModel: global schreibt config.ini [gc] und lässt den Rest stehen") {
    const std::string cfg = "; Kommentar\n[ui]\nlanguage = de\n";
    OptionsModel m(Console::Gc, "", cfg, "");
    m.step(rowOf(m, "progressive"), +1);
    CHECK(m.configIniText() == cfg + "\n[gc]\nprogressive = 1\n");
    CHECK(m.gamesIniText().empty());
}

TEST_CASE("OptionsModel: reset entfernt den Spielabschnitt, N64-Abschnitt bleibt Byte für Byte") {
    const std::string n64 = "[n64_method]\r\nn64/Super Mario 64.z64 = wii64\r\n; Notiz\r\n";
    OptionsModel m(Console::Wii, "RMGP01", "", n64 + "[game RMGP01]\r\nvideo = pal60\r\n");
    CHECK_FALSE(m.inherited(rowOf(m, "video")));
    m.reset();
    CHECK(m.gamesIniText() == n64);
    CHECK(m.inherited(rowOf(m, "video")));
    CHECK(m.dirty());
}

TEST_CASE("OptionsModel: Option, die die Nintendont-Version nicht kennt, ist gesperrt") {
    OptionsModel m(Console::Gc, "GM8P01", "", "", NinVersion{4, 400});
    const std::size_t r = rowOf(m, "skip_ipl");  // ab 4.434
    CHECK_FALSE(m.enabled(r));
    m.step(r, +1);
    CHECK_FALSE(m.dirty());
    CHECK(m.enabled(rowOf(m, "video")));
    CHECK(OptionsModel(Console::Gc, "GM8P01", "", "").enabled(r));  // Version unbekannt: nichts gesperrt
}

TEST_CASE("OptionsModel: ios kennt 200–255, ein fremder return_to-Wert bleibt wählbar") {
    const OptionsModel m(Console::Wii, "RMGP01", "", "[game RMGP01]\nreturn_to = 0001000148415858\n");
    const auto ios = m.choices(rowOf(m, "ios"));
    CHECK(ios.size() == 1 + 1 + 56);  // default, auto, 200..255
    CHECK(ios.back() == "255");
    const std::size_t r = rowOf(m, "return_to");
    CHECK(m.shownValue(r) == "0001000148415858");
    CHECK(m.choices(r).back() == "0001000148415858");
    CHECK(m.choice(r) == m.choices(r).size() - 1);
}

TEST_CASE("OptionsModel: set für den Smoke, unbekannte Schlüssel und Werte scheitern") {
    OptionsModel m(Console::Wii, "RMGP01", "", "");
    CHECK(m.set("Video", "PAL60"));
    CHECK(m.gamesIniText() == "[game RMGP01]\nvideo = pal60\n");
    CHECK_FALSE(m.set("video", "secam"));
    CHECK_FALSE(m.set("bogus", "1"));
}

TEST_CASE("optionsFileText: fehlende Datei ist leer, Lesefehler sperrt das Speichern") {
    const std::vector<u8> bytes{'[', 'a', ']', '\n'};
    CHECK(optionsFileText(Result<std::vector<u8>>::ok(bytes)) == std::optional<std::string>("[a]\n"));
    CHECK(optionsFileText(Result<std::vector<u8>>::err({ErrorCode::NotFound, "games.ini"})) ==
          std::optional<std::string>(""));
    CHECK_FALSE(optionsFileText(Result<std::vector<u8>>::err({ErrorCode::IoError, "games.ini"})).has_value());
    CHECK_FALSE(optionsFileText(Result<std::vector<u8>>::err({ErrorCode::InvalidData, "zu groß"})).has_value());
}

TEST_CASE("OptionsModel: reset behält die Kindersicherungs-Sperre locked = 1 (M17-Review)") {
    OptionsModel m(Console::Wii, "RMGP01", "", "[game RMGP01]\nvideo = pal60\nlocked = 1\n");
    m.reset();
    CHECK(m.gamesIniText().find("locked = 1") != std::string::npos);
    CHECK(m.gamesIniText().find("video") == std::string::npos);
    CHECK(m.inherited(rowOf(m, "video")));
    OptionsModel plain(Console::Wii, "RMGP01", "", "[game RMGP01]\nvideo = pal60\n");
    plain.reset();
    CHECK(plain.gamesIniText().find("RMGP01") == std::string::npos);  // ohne Sperre: Abschnitt ganz weg
}
