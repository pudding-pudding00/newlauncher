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
