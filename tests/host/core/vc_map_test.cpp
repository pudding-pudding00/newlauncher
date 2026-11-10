#include "core/ini.hpp"
#include "core/vc_map.hpp"
#include <doctest/doctest.h>
#include <string>
#include <vector>
using namespace nl;

TEST_CASE("parseTitleId") {
    CHECK(parseTitleId("000100014E414D50") == 0x000100014E414D50ULL);
    CHECK(parseTitleId("000100014e414d50") == 0x000100014E414D50ULL);
    CHECK(parseTitleId(" 000100014E414D50 ") == 0x000100014E414D50ULL);
    CHECK_FALSE(parseTitleId("000100014E414D5").has_value());
    CHECK_FALSE(parseTitleId("000100014E414D500").has_value());
    CHECK_FALSE(parseTitleId("0x0001000144414D50").has_value());
    CHECK_FALSE(parseTitleId("00010001 4E414D50").has_value());
    CHECK_FALSE(parseTitleId("00010001ZZZZZZZZ").has_value());
    CHECK_FALSE(parseTitleId("").has_value());
}

TEST_CASE("formatTitleId") {
    CHECK(formatTitleId(0x000100014E414D50ULL) == "000100014E414D50");
    CHECK(formatTitleId(1) == "0000000000000001");
}

TEST_CASE("VcMap liest Spiel-Schlüssel und bloße Dateinamen") {
    const VcMap m = VcMap::fromIni(IniDoc::parse(
        "[map]\nSuper Mario 64 (Europe).z64 = 000100014E414D50\n"
        "n64/Mario Kart 64 (Europe).z64 = 000100014E4B5450\nn64/kaputt.z64 = 123\n"));
    CHECK(m.titleFor("n64/Super Mario 64 (Europe).z64") == 0x000100014E414D50ULL);
    CHECK(m.titleFor("n64/Mario Kart 64 (Europe).z64") == 0x000100014E4B5450ULL);
    CHECK_FALSE(m.titleFor("n64/kaputt.z64").has_value());
    CHECK(m.size() == 2);
}

TEST_CASE("VcMap schreibt Spiel-Schlüssel, löschen per nullopt") {
    VcMap m;
    m.set("n64/Super Mario 64 (Europe).z64", 0x000100014E414D50ULL);
    m.set("n64/weg.z64", 0x0001000100000001ULL);
    m.set("n64/weg.z64", std::nullopt);
    const std::string text = m.toIni().serialize();
    CHECK(text.find("n64/Super Mario 64 (Europe).z64 = 000100014E414D50") != std::string::npos);
    CHECK(text.find("weg") == std::string::npos);
    CHECK(m.size() == 1);
}

TEST_CASE("channelDisplayName fällt auf die Title-ID zurück") {
    CHECK(channelDisplayName({0x000100014E4C5443ULL, "NL Testkanal"}) == "NL Testkanal");
    CHECK(channelDisplayName({0x000100014E4C5443ULL, ""}) == "000100014E4C5443");
}

TEST_CASE("suggestChannel findet den passenden Kanal") {
    const std::vector<ChannelInfo> ch{{1, "Mario Kart 64"}, {2, "Super Mario 64"}, {3, "The Legend of Zelda"}};
    CHECK(suggestChannel("Super Mario 64", ch) == 1u);
    CHECK(suggestChannel("Mario Kart 64", ch) == 0u);
    CHECK_FALSE(suggestChannel("Zelda Ocarina of Time", ch).has_value());
    CHECK_FALSE(suggestChannel("Pilotwings 64", ch).has_value());
}

TEST_CASE("Namensabgleich faltet Akzente und Tags") {
    CHECK(vcMatchScore("Pokémon Snap", "POKEMON SNAP") == doctest::Approx(1.0));
    CHECK(vcMatchScore("Super Mario 64 (Europe) (En,Fr,De)", "Super Mario 64") == doctest::Approx(1.0));
}

TEST_CASE("Namensabgleich mit leeren Eingaben") {
    CHECK(vcMatchScore("", "x") == 0.0f);
    CHECK(vcMatchScore("(Europe)", "Europe") == 0.0f);
    CHECK_FALSE(suggestChannel("Super Mario 64", std::vector<ChannelInfo>{}).has_value());
    CHECK_FALSE(suggestChannel("Super Mario 64", std::vector<ChannelInfo>{{1, ""}}).has_value());
}
