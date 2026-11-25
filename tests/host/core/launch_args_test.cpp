#include "core/launch_args.hpp"
#include <doctest/doctest.h>
#include <string>
#include <vector>
using namespace nl;

namespace {
constexpr std::string_view kDol = "sd:/apps/fceugx/boot.dol";
constexpr std::string_view kRom = "sd:/roms/nes/Zelda (Europe) (Rev 1).nes";

u32 be32(const std::vector<u8>& b, std::size_t at) {
    return (u32(b[at]) << 24) | (u32(b[at + 1]) << 16) | (u32(b[at + 2]) << 8) | u32(b[at + 3]);
}
}  // namespace

TEST_CASE("{dir}|{file} teilt den ROM-Pfad am letzten Schrägstrich") {
    const auto a = expandArgs(kDol, "{dir}|{file}", kRom);
    CHECK(a == std::vector<std::string>{"sd:/apps/fceugx/boot.dol", "sd:/roms/nes/", "Zelda (Europe) (Rev 1).nes"});
}

TEST_CASE("{path} liefert den vollen Pfad") {
    const auto a = expandArgs(kDol, "{path}", kRom);
    REQUIRE(a.size() == 2);
    CHECK(a[1] == kRom);
}

TEST_CASE("leeres Schema → nur argv[0]") {
    CHECK(expandArgs(kDol, "", kRom) == std::vector<std::string>{std::string(kDol)});
}

TEST_CASE("unbekannter Platzhalter {foo} und Text um Platzhalter bleiben wörtlich") {
    const auto a = expandArgs(kDol, "--rom={file}|{foo}|{file}{file}", kRom);
    REQUIRE(a.size() == 4);
    CHECK(a[1] == "--rom=Zelda (Europe) (Rev 1).nes");
    CHECK(a[2] == "{foo}");
    CHECK(a[3] == "Zelda (Europe) (Rev 1).nesZelda (Europe) (Rev 1).nes");
}

TEST_CASE("packArgvBlock: feste Bytes") {
    const std::vector<std::string> args{"a:/x", "y"};
    auto r = packArgvBlock(args, 0x93300800);
    REQUIRE(r.isOk());
    const std::vector<u8>& b = r.value();
    REQUIRE(b.size() == 24 + 8);
    CHECK(be32(b, 0) == 0x5f617267);
    CHECK(be32(b, 4) == 0x93300818);
    CHECK(be32(b, 8) == 7);
    CHECK(be32(b, 12) == 0);
    CHECK(be32(b, 16) == 0);
    CHECK(be32(b, 20) == 0);
    const std::vector<u8> tail(b.begin() + 24, b.end());
    CHECK(tail == std::vector<u8>{'a', ':', '/', 'x', 0, 'y', 0, 0});
}

TEST_CASE("packArgvBlock: Block größer als 0x800 → InvalidData") {
    const std::vector<std::string> fits{std::string(kMaxArgvBytes - 24 - 2, 'x')};
    CHECK(packArgvBlock(fits, 0x93300800).isOk());
    const std::vector<std::string> tooBig{std::string(kMaxArgvBytes - 24 - 1, 'x')};
    auto r = packArgvBlock(tooBig, 0x93300800);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().code == ErrorCode::InvalidData);
}

TEST_CASE("Bytes ≥ 0x80 (UTF-8-Umlaut) gehen unverändert durch") {
    const std::vector<std::string> args{"\xC3\xA4"};  // „ä“
    auto r = packArgvBlock(args, 0x93300800);
    REQUIRE(r.isOk());
    CHECK(r.value()[24] == 0xC3);
    CHECK(r.value()[25] == 0xA4);
    const auto a = expandArgs(kDol, "{file}", "sd:/roms/gb/P\xC3\xB6kemon.gb");
    CHECK(a[1] == "P\xC3\xB6kemon.gb");
}

// M21: Startargumente prüfen (--remote, --from-game).
TEST_CASE("hasArgFlag: an jeder Stelle, nullptr sicher") {
    const char* a[] = {"sd:/apps/newlauncher/boot.dol", "--remote", "--from-game"};
    CHECK(nl::hasArgFlag(3, a, "--from-game"));
    CHECK(nl::hasArgFlag(3, a, "--remote"));
    CHECK_FALSE(nl::hasArgFlag(2, a, "--from-game"));
    CHECK_FALSE(nl::hasArgFlag(0, nullptr, "--from-game"));
}
