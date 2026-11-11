#include "core/ios_info.hpp"
#include <doctest/doctest.h>

#include <array>
#include <cstring>
using namespace nl;

namespace {
// iosinfo_t aus d2x (cios.h): magic, magicversion, version, baseios, name[16], versionstring[16]; Big-Endian.
std::array<u8, 0x40> info(u32 magic, u32 base, const char* name, const char* ver, u32 magicVersion = 1) {
    std::array<u8, 0x40> b{};
    const auto put = [&](std::size_t at, u32 v) {
        b[at] = u8(v >> 24);
        b[at + 1] = u8(v >> 16);
        b[at + 2] = u8(v >> 8);
        b[at + 3] = u8(v);
    };
    put(0, magic);
    put(4, magicVersion);
    put(8, 10);
    put(12, base);
    std::strncpy(reinterpret_cast<char*>(&b[16]), name, 16);
    std::strncpy(reinterpret_cast<char*>(&b[32]), ver, 16);
    return b;
}
}  // namespace

TEST_CASE("ios_info: d2x v10 Basis 56") {
    auto i = parseIosInfo(info(0x1EE7C105, 56, "d2x", "v10"));
    REQUIRE(i.has_value());
    CHECK(i->version == 10);
    CHECK(i->baseios == 56);
    CHECK(i->name == "d2x");
    CHECK(i->versionString == "v10");
    const CiosStatus s = classifyCios(true, i);
    CHECK(s.state == CiosState::D2x);
    CHECK(s.base == 56);
    CHECK(s.version == "v10");
}

TEST_CASE("ios_info: Basis 57 wird übernommen") {
    const CiosStatus s = classifyCios(true, parseIosInfo(info(0x1EE7C105, 57, "d2x", "v11-beta1")));
    CHECK(s.state == CiosState::D2x);
    CHECK(s.base == 57);
}

TEST_CASE("ios_info: falsche Magic") {
    CHECK_FALSE(parseIosInfo(info(0x12345678, 56, "d2x", "v10")).has_value());
    CHECK_FALSE(parseIosInfo(info(0x1EE7C105, 56, "d2x", "v10", 2)).has_value());  // unbekannte magicversion
    auto shortBuf = info(0x1EE7C105, 56, "d2x", "v10");
    CHECK_FALSE(parseIosInfo(std::span<const u8>(shortBuf).first(0x20)).has_value());
}

TEST_CASE("ios_info: Namen ohne NUL werden auf 16 Zeichen begrenzt") {
    auto b = info(0x1EE7C105, 56, "", "");
    std::memset(&b[16], 'A', 16);
    std::memset(&b[32], 'B', 16);
    auto i = parseIosInfo(b);
    REQUIRE(i.has_value());
    CHECK(i->name == std::string(16, 'A'));
    CHECK(i->versionString == std::string(16, 'B'));
}

TEST_CASE("ios_info: kein TMD") {
    const CiosStatus s = classifyCios(false, parseIosInfo(info(0x1EE7C105, 56, "d2x", "v10")));
    CHECK(s.state == CiosState::Missing);
}

TEST_CASE("ios_info: Info unlesbar") {
    const CiosStatus s = classifyCios(true, std::nullopt);
    CHECK(s.state == CiosState::Unverified);
    CHECK(s.base == 56);
}

TEST_CASE("ios_info: Waninkoko") {
    const CiosStatus s = classifyCios(true, parseIosInfo(info(0x1EE7C105, 38, "cios", "rev21")));
    CHECK(s.state == CiosState::NotD2x);
    CHECK(s.version == "cios");  // Kontext für den Dialog „Slot 249 enthält %s“
}
