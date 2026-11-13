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

// --- M15: Cheats (Nintendont c683db3 kernel/Patch.c Check_Cheats: CheatPath ohne Gerät, auf dem Spielgerät) ---
namespace {
std::string cheatPathAt(const std::array<u8, kNinCfgSize>& b) {
    std::string s;
    for (std::size_t i = 0x113; i < 0x212 && b[i] != 0; ++i) s.push_back(static_cast<char>(b[i]));
    return s;
}
}  // namespace

TEST_CASE("nin_cfg: CheatPath setzt CHEATS und CHEAT_PATH und steht hinter GamePath") {
    NinConfigInput in{"/games/x [GM8P01]/game.iso", "GM8P01", false};
    in.cheatPath = "/newlauncher/cache/GM8P01.gct";
    auto r = buildNinCfg(in);
    REQUIRE(r.isOk());
    CHECK(be32(r.value(), 0x008) == (0x00000088u | kNinCfgCheats | kNinCfgCheatPath));
    CHECK(cheatPathAt(r.value()) == "/newlauncher/cache/GM8P01.gct");
    CHECK(pathAt(r.value()) == "/games/x [GM8P01]/game.iso");
    // ohne CheatPath keine Cheat-Bits, Feld bleibt leer
    auto none = buildNinCfg({"/games/x [GM8P01]/game.iso", "GM8P01", false});
    REQUIRE(none.isOk());
    CHECK((be32(none.value(), 0x008) & (kNinCfgCheats | kNinCfgCheatPath)) == 0);
    CHECK(cheatPathAt(none.value()).empty());
}

TEST_CASE("nin_cfg: CheatPath mit 254 Byte geht, 255 → PathTooLong") {
    NinConfigInput in{"/g.iso", "GM8P01", false};
    const std::string p254 = "/" + std::string(253, 'c');
    const std::string p255 = "/" + std::string(254, 'c');
    in.cheatPath = p254;
    auto ok = buildNinCfg(in);
    REQUIRE(ok.isOk());
    CHECK(cheatPathAt(ok.value()) == p254);
    CHECK(ok.value()[0x113 + 254] == 0);
    CHECK(be32(ok.value(), 0x214) == 4u);  // MaxPads unberührt
    in.cheatPath = p255;
    auto bad = buildNinCfg(in);
    REQUIRE_FALSE(bad.isOk());
    CHECK(bad.error() == NinCfgError::PathTooLong);
}

TEST_CASE("nin_cfg: ninCheatPath liegt im Cache des Spielgeräts") {
    CHECK(ninCheatPath("GM8P01") == "/newlauncher/cache/GM8P01.gct");
}

// --- Task 2: Version aus der Nintendont-DOL ---
#include "support/mem_fs.hpp"

namespace {
constexpr const char* kNinDol = "sd:/apps/nintendont/boot.dol";

test::MemFileSystem fsWithTag(std::size_t size, std::size_t at, std::string_view tag) {
    std::vector<u8> img(size, 0xA5);
    for (std::size_t i = 0; i < tag.size(); ++i) img[at + i] = static_cast<u8>(tag[i]);
    test::MemFileSystem fs;
    fs.addFile(kNinDol, std::move(img));
    return fs;
}
}  // namespace

TEST_CASE("nin_cfg: Version gefunden") {
    auto fs = fsWithTag(4096, 100, "$$Version:6.509");
    auto v = findNintendontVersion(fs, kNinDol);
    REQUIRE(v.has_value());
    CHECK(v->major == 6);
    CHECK(v->minor == 509);
    CHECK(formatNinVersion(*v) == "6.509");
}

TEST_CASE("nin_cfg: Version auf der Blockgrenze") {
    for (std::size_t at : {65536u - 5u, 65536u - 10u, 65536u - 12u, 65536u}) {
        CAPTURE(at);
        auto fs = fsWithTag(200000, at, "$$Version:6.509");
        auto v = findNintendontVersion(fs, kNinDol);
        REQUIRE(v.has_value());
        CHECK(*v == NinVersion{6, 509});
    }
}

TEST_CASE("nin_cfg: kein Versionsstring") {
    auto fs = fsWithTag(100000, 0, "");
    CHECK_FALSE(findNintendontVersion(fs, kNinDol).has_value());
    test::MemFileSystem empty;
    CHECK_FALSE(findNintendontVersion(empty, kNinDol).has_value());
}

TEST_CASE("nin_cfg: $$Version: ohne Zahl") {
    auto a = fsWithTag(4096, 10, "$$Version:x.1");
    CHECK_FALSE(findNintendontVersion(a, kNinDol).has_value());
    auto b = fsWithTag(4096, 10, "$$Version:6.");
    CHECK_FALSE(findNintendontVersion(b, kNinDol).has_value());
    auto c = fsWithTag(4096, 4096 - 12, "$$Version:6.");  // Dateiende mitten in der Zahl
    CHECK_FALSE(findNintendontVersion(c, kNinDol).has_value());
}

TEST_CASE("nin_cfg: misslungener Treffer, Suche geht weiter") {
    // Ein nacktes Suchmuster vor dem Build-String darf die Erkennung nicht verhindern (Review M8).
    auto same = fsWithTag(4096, 10, "$$Version:");
    {
        std::vector<u8> img(4096, 0xA5);
        const std::string_view first = "$$Version:", real = "$$Version:6.509";
        std::copy(first.begin(), first.end(), img.begin() + 10);
        std::copy(real.begin(), real.end(), img.begin() + 900);
        same = test::MemFileSystem{};
        same.addFile(kNinDol, std::move(img));
    }
    auto v = findNintendontVersion(same, kNinDol);
    REQUIRE(v.has_value());
    CHECK(*v == NinVersion{6, 509});

    std::vector<u8> img(200000, 0xA5);  // echter Tag erst im übernächsten Block
    const std::string_view bad = "$$Version:x", real = "$$Version:6.510";
    std::copy(bad.begin(), bad.end(), img.begin() + 10);
    std::copy(real.begin(), real.end(), img.begin() + 150000);
    test::MemFileSystem far;
    far.addFile(kNinDol, std::move(img));
    auto w = findNintendontVersion(far, kNinDol);
    REQUIRE(w.has_value());
    CHECK(*w == NinVersion{6, 510});
}

TEST_CASE("nin_cfg: Vergleich") {
    CHECK(NinVersion{6, 492} < kMinNintendont);
    CHECK(NinVersion{6, 493} >= kMinNintendont);
    CHECK(NinVersion{7, 0} > kMinNintendont);
}

// --- M14 Task 1: Optionsbits und Mindestversionen (CommonConfig.h, Nintendont c683db3) ---
#include <cstring>

TEST_CASE("nin_cfg: Config-Bits wie CommonConfig.h") {
    CHECK(kNinCfgCheats == 1u << 0);
    CHECK(kNinCfgMemcardEmu == 1u << 3);
    CHECK(kNinCfgCheatPath == 1u << 4);
    CHECK(kNinCfgForceWide == 1u << 5);
    CHECK(kNinCfgForceProg == 1u << 6);
    CHECK(kNinCfgAutoBoot == 1u << 7);
    CHECK(kNinCfgUsb == 1u << 10);
    CHECK(kNinCfgMcMulti == 1u << 13);
    CHECK(kNinCfgNativeSi == 1u << 14);
    CHECK(kNinCfgWiiuWide == 1u << 15);
    CHECK(kNinCfgSkipIpl == 1u << 18);
}

TEST_CASE("nin_cfg: Video- und Sprachwerte wie CommonConfig.h") {
    CHECK(kNinVidAuto == 0u);
    CHECK(kNinVidForce == 1u << 16);
    CHECK(kNinVidNone == 2u << 16);
    CHECK(kNinVidPal50 == 1u << 0);
    CHECK(kNinVidPal60 == 1u << 1);
    CHECK(kNinVidNtsc == 1u << 2);
    CHECK(kNinVidMpal == 1u << 3);
    CHECK(kNinVidProg == 1u << 4);
    CHECK(kNinLangAuto == 0xFFFFFFFFu);
    CHECK(kNinLangEn == 0u);
    CHECK(kNinLangDe == 1u);
    CHECK(kNinLangFr == 2u);
    CHECK(kNinLangEs == 3u);
    CHECK(kNinLangIt == 4u);
    CHECK(kNinLangNl == 5u);
}

TEST_CASE("nin_cfg: Optionen landen an den Offsets") {
    NinConfigInput in{"/games/x [GM8P01]/game.iso", "GM8P01", false};
    in.extraFlags = kNinCfgForceWide;
    in.videoMode = kNinVidForce | kNinVidPal60;
    in.language = 1;
    in.maxPads = 2;
    in.memcardBlocks = 3;
    auto r = buildNinCfg(in);
    REQUIRE(r.isOk());
    const auto& b = r.value();
    CHECK(be32(b, 0x008) == (0x00000088u | (1u << 5)));
    CHECK(be32(b, 0x00C) == 0x00010002u);
    CHECK(be32(b, 0x010) == 1u);
    CHECK(be32(b, 0x214) == 2u);
    CHECK(b[0x21C] == 3);
}

TEST_CASE("nin_cfg: Standardeingabe bleibt byte-gleich zu M8") {
    const std::string path = "/games/Metroid Prime [GM8P01]/game.iso";
    NinConfigInput in{path, "GM8P01", false};
    CHECK(in.extraFlags == 0u);
    CHECK(in.videoMode == kNinVidAuto);
    CHECK(in.language == kNinLangAuto);
    CHECK(in.maxPads == 4u);
    CHECK(in.memcardBlocks == 2u);
    auto r = buildNinCfg(in);
    REQUIRE(r.isOk());
    std::array<u8, kNinCfgSize> golden{};
    const u8 head[] = {0x01, 0x07, 0x0C, 0xF6, 0, 0, 0, 10, 0, 0, 0, 0x88, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF};
    std::memcpy(golden.data(), head, sizeof head);
    std::memcpy(golden.data() + 0x014, path.data(), path.size());
    golden[0x217] = 4;
    std::memcpy(golden.data() + 0x218, "GM8P", 4);
    golden[0x21C] = 2;
    CHECK(r.value() == golden);
}

TEST_CASE("nin_cfg: Mindestversion je Option") {
    auto gate = [](std::string_view key) -> std::optional<NinVersion> {
        for (const NinOptionGate& g : ninOptionGates())
            if (key == g.key) return g.minVersion;
        return std::nullopt;
    };
    CHECK(gate("force_wide") == NinVersion{1, 109});
    CHECK(gate("progressive") == NinVersion{1, 109});
    CHECK(gate("video") == NinVersion{1, 109});
    CHECK(gate("memcard") == NinVersion{1, 135});
    CHECK(gate("memcard_blocks") == NinVersion{1, 135});
    CHECK(gate("native_si") == NinVersion{2, 189});
    CHECK(gate("wiiu_wide") == NinVersion{2, 258});
    CHECK(gate("skip_ipl") == NinVersion{4, 434});
    CHECK(gate("language") == NinVersion{1, 0});
    CHECK(gate("max_pads") == NinVersion{1, 0});
    CHECK_FALSE(gate("unbekannt").has_value());
}
