#include "core/nin_cfg.hpp"
#include <doctest/doctest.h>
#include <string>
using namespace nl;

namespace {
u32 be32(const std::array<u8, kNinCfgSize>& b, std::size_t off) {
    return (u32{b[off]} << 24) | (u32{b[off + 1]} << 16) | (u32{b[off + 2]} << 8) | u32{b[off + 3]};
}
std::string pathAt(const std::array<u8, kNinCfgSize>& b) {
    std::string s;
    for (std::size_t i = 0x014; i < 0x113 && b[i] != 0; ++i) s.push_back(static_cast<char>(b[i]));
    return s;
}
}  // namespace

TEST_CASE("nin_cfg: Golden-Block SD") {
    const std::string path = "/games/Metroid Prime [GM8P01]/game.iso";
    auto r = buildNinCfg({path, "GM8P01", false});
    REQUIRE(r.isOk());
    const auto& b = r.value();
    CHECK(be32(b, 0x000) == 0x01070CF6u);
    CHECK(be32(b, 0x004) == 10u);
    CHECK(be32(b, 0x008) == 0x00000088u);
    CHECK(be32(b, 0x00C) == 0u);
    CHECK(be32(b, 0x010) == 0xFFFFFFFFu);
    CHECK(pathAt(b) == path);
    for (std::size_t i = 0x014 + path.size(); i < 0x113; ++i) CHECK(b[i] == 0);
    for (std::size_t i = 0x113; i < 0x214; ++i) CHECK(b[i] == 0);
    CHECK(be32(b, 0x214) == 4u);
    CHECK(be32(b, 0x218) == 0x474D3850u);
    CHECK(b[0x21C] == 2);
    CHECK(b[0x21D] == 0);
    CHECK(b[0x21E] == 0);
    CHECK(b[0x21F] == 0);
    CHECK(be32(b, 0x220) == 0u);
}

TEST_CASE("nin_cfg: USB setzt Bit 10") {
    auto r = buildNinCfg({"/games/x [GM8P01]/game.iso", "GM8P01", true});
    REQUIRE(r.isOk());
    CHECK(be32(r.value(), 0x008) == 0x00000488u);
}

TEST_CASE("nin_cfg: Pfad mit 254 Byte geht, 255 nicht") {
    const std::string p254 = "/" + std::string(253, 'a');
    const std::string p255 = "/" + std::string(254, 'a');
    auto ok = buildNinCfg({p254, "GM8P01", false});
    REQUIRE(ok.isOk());
    CHECK(pathAt(ok.value()) == p254);
    CHECK(ok.value()[0x014 + 254] == 0);
    auto bad = buildNinCfg({p255, "GM8P01", false});
    REQUIRE_FALSE(bad.isOk());
    CHECK(bad.error() == NinCfgError::PathTooLong);
}

TEST_CASE("nin_cfg: kurze ID ist ein Fehler") {
    auto bad = buildNinCfg({"/g.iso", "GM8", false});
    REQUIRE_FALSE(bad.isOk());
    CHECK(bad.error() == NinCfgError::BadId);
}

TEST_CASE("nin_cfg: UTF-8 bytegenau") {
    const std::string path = "/games/Pok\xC3\xA9mon Colosseum [GC6P01]/game.iso";
    auto r = buildNinCfg({path, "GC6P01", false});
    REQUIRE(r.isOk());
    CHECK(pathAt(r.value()) == path);
    CHECK(be32(r.value(), 0x218) == 0x47433650u);
}

TEST_CASE("nin_cfg: ninGamePath") {
    CHECK(ninGamePath("games/x/game.iso") == "/games/x/game.iso");
    CHECK(ninGamePath("/games/x/game.iso") == "/games/x/game.iso");
    CHECK(ninGamePath("games\\x\\game.iso") == "/games/x/game.iso");
    CHECK(ninGamePath("//games/x/game.iso") == "/games/x/game.iso");
}
