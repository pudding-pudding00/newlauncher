#include "core/gecko_txt.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace nl;

namespace {
// Im Test erzeugte Datei, keine echte Codeliste.
const char* kSample =
    "RMGP01\n"
    "Testspiel\n"
    "\n"
    "Unendlich Leben [Tester]\n"
    "04001234 00000063\n"
    "* Setzt Leben auf 99\n"
    "\n"
    "Zwei Zeilen\n"
    "04001000 00000001\n"
    "C2002000 00000001\n"
    "60000000 00000000\n"
    "\n"
    "Mit Wert\n"
    "0400XXXX 00000001\n"
    "Wert eintragen\n";
}  // namespace

TEST_CASE("parseGeckoTxt: Kopf, drei Blöcke, Autor und Kommentar") {
    const auto r = parseGeckoTxt(kSample);
    REQUIRE(r.isOk());
    const GeckoFile& f = r.value();
    CHECK(f.id6 == "RMGP01");
    CHECK(f.title == "Testspiel");
    CHECK(f.skipped == 0);
    REQUIRE(f.codes.size() == 3);
    CHECK(f.codes[0].name == "Unendlich Leben");
    CHECK(f.codes[0].author == "Tester");
    CHECK(f.codes[0].words == std::vector<u32>{0x04001234, 0x00000063});
    CHECK(f.codes[0].comment == "* Setzt Leben auf 99");
    CHECK_FALSE(f.codes[0].hasPlaceholder);
    CHECK(f.codes[1].name == "Zwei Zeilen");
    CHECK(f.codes[1].author.empty());
    CHECK(f.codes[1].words.size() == 6);
    CHECK(f.codes[2].hasPlaceholder);
    CHECK(f.codes[2].comment == "Wert eintragen");
}

TEST_CASE("parseGeckoTxt: CRLF, BOM und Tabs") {
    const std::string text = "\xEF\xBB\xBFRMGP01\r\nTestspiel\r\n\r\nCode\r\n04001234\t00000063\r\n";
    const auto r = parseGeckoTxt(text);
    REQUIRE(r.isOk());
    CHECK(r.value().id6 == "RMGP01");
    CHECK(r.value().title == "Testspiel");
    REQUIRE(r.value().codes.size() == 1);
    CHECK(r.value().codes[0].name == "Code");
    CHECK(r.value().codes[0].words == std::vector<u32>{0x04001234, 0x00000063});
}

TEST_CASE("parseGeckoTxt: kaputte Blöcke werden übersprungen und gezählt") {
    const std::string text =
        "RMGP01\nT\n\n"
        "Halbe Zeile\n04001234\n\n"       // ungerade Zeilenhälfte
        "04001234 00000001\n\n"           // Code ohne Namen
        "Kurzer Wert\n04001234 0001\n\n"  // zweite Hälfte zu kurz
        "Gut\n04001234 00000001\n";
    const auto r = parseGeckoTxt(text);
    REQUIRE(r.isOk());
    CHECK(r.value().skipped == 3);
    REQUIRE(r.value().codes.size() == 1);
    CHECK(r.value().codes[0].name == "Gut");
}

TEST_CASE("parseGeckoTxt: andere Platzhalterbuchstaben und Kommentar hinter der Codezeile") {
    const auto r = parseGeckoTxt(
        "RMGP01\nT\n\n"
        "Wert\n0442BBDC ZZZZzzzz\n\n"
        "Wert2\n906400A8 398000KK\n\n"
        "Mit Autor dahinter\n00FDCD1C 00000037 [Tester]\n");
    REQUIRE(r.isOk());
    const auto& c = r.value().codes;
    REQUIRE(c.size() == 3);
    CHECK(c[0].hasPlaceholder);
    CHECK(c[1].hasPlaceholder);
    CHECK_FALSE(c[2].hasPlaceholder);
    CHECK(c[2].words == std::vector<u32>{0x00FDCD1C, 0x00000037});
}

TEST_CASE("parseGeckoTxt: mehrdeutige Hex-Zeile wird Kommentar und macht den Code unwählbar") {
    const auto r = parseGeckoTxt(
        "RMGP01\nT\n\n"
        "Tempo\n04001234 00000000\n43960000 = 300 km/h\n\n"
        "Halber Rest\n04001234 00000001\n04001238 0001\n\n"
        "Wort am Zeilenanfang\n04001234 00000001\nInfinite Energies\n");
    REQUIRE(r.isOk());
    const auto& c = r.value().codes;
    REQUIRE(c.size() == 3);
    CHECK(c[0].hasPlaceholder);
    CHECK(c[0].comment == "43960000 = 300 km/h");
    CHECK(c[0].words.size() == 2);
    CHECK(c[1].hasPlaceholder);
    CHECK(c[1].words.size() == 2);
    CHECK_FALSE(c[2].hasPlaceholder);  // „Infinite“ hat keine Ziffer, ist also Text
    CHECK(c[2].comment == "Infinite Energies");
}

TEST_CASE("parseGeckoTxt: Latin-1 wird nach UTF-8 umgewandelt, gültiges UTF-8 bleibt") {
    const auto l1 = parseGeckoTxt("RSBP01\nT\n\nCaf\xE9 [Ren\xE9]\n04001234 00000001\n");
    REQUIRE(l1.isOk());
    REQUIRE(l1.value().codes.size() == 1);
    CHECK(l1.value().codes[0].name == "Caf\xC3\xA9");
    CHECK(l1.value().codes[0].author == "Ren\xC3\xA9");
    const auto utf = parseGeckoTxt("RSBP01\nT\n\nCaf\xC3\xA9 ?\n04001234 00000001\n");
    REQUIRE(utf.isOk());
    REQUIRE(utf.value().codes.size() == 1);
    CHECK(utf.value().codes[0].name == "Caf\xC3\xA9 ?");
}

TEST_CASE("parseGeckoTxt: Name ohne Codezeilen zählt als übersprungen") {
    const auto r = parseGeckoTxt("RMGP01\nT\n\nNur Name\n\nGut\n04001234 00000001\n");
    REQUIRE(r.isOk());
    CHECK(r.value().skipped == 1);
    CHECK(r.value().codes.size() == 1);
}

TEST_CASE("parseGeckoTxt: großer Kommentarblock bricht nicht ab") {
    std::string text = "RMGP01\nT\n\nCode\n04001234 00000001\n";
    const std::string line(99, 'k');
    while (text.size() < 1024 * 1024) text += line + "\n";
    const auto r = parseGeckoTxt(text);
    REQUIRE(r.isOk());
    REQUIRE(r.value().codes.size() == 1);
    CHECK(r.value().codes[0].comment.size() > 1000000);
}

TEST_CASE("parseGeckoTxt: leer oder HTML ergibt InvalidData") {
    CHECK(parseGeckoTxt("").error().code == ErrorCode::InvalidData);
    CHECK(parseGeckoTxt("<!DOCTYPE html>\n<html><body>404</body></html>\n").error().code == ErrorCode::InvalidData);
    CHECK(parseGeckoTxt("RMGP0\nT\n").error().code == ErrorCode::InvalidData);
    CHECK(parseGeckoTxt("RMGP01 x\nT\n").error().code == ErrorCode::InvalidData);
}

TEST_CASE("parseGeckoTxt: nur Kopf ist gültig, ohne Codes") {
    const auto r = parseGeckoTxt("RMGP01\nTestspiel\n");
    REQUIRE(r.isOk());
    CHECK(r.value().codes.empty());
}
