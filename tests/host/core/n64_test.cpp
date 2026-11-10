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
