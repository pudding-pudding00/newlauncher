#include "core/save_query.hpp"
#include "core/nin_cfg.hpp"
#include <doctest/doctest.h>
using namespace nl;

namespace {
Game game(Console c, const char* key, const char* path, u8 device = 0) {
    return Game{c, key, "Titel", 0, 0, nullptr, path, 0, 0, device};
}

EmuNandInfo emuNand() {
    EmuNandInfo i;
    i.status = EmuNandStatus::Ok;
    i.path = EmuNandPath{"usb", "/emunand"};
    return i;
}

LaunchTarget wiiTarget(bool emu, bool full = false) {
    LaunchTarget t;
    t.kind = LaunchTarget::Kind::Wii;
    t.wii = WiiBootPlan{};
    if (emu) t.wii->emuNand = emuNand();
    t.wii->emuNandFull = full;
    return t;
}

LaunchTarget gcTarget(bool real, u32 flags) {
    LaunchTarget t;
    t.kind = LaunchTarget::Kind::Dol;
    t.nintendont = NinVersion{};
    t.gc = GcOptions{};
    t.gc->memcardReal = real;
    t.gc->nin.extraFlags = flags;
    return t;
}

EmulatorTable table() {
    EmulatorTable t = EmulatorTable::fromIni(IniDoc::parse("[n64_not64]\nsave_dir = sd:/not64/saves\nsave_ext = eep\n"));
    EmulatorEntry snes = *t.section("snes");
    snes.profile = "snes9xgx";
    t.set("snes", snes);
    EmulatorEntry not64 = *t.section("n64_not64");
    not64.profile = "not64";
    t.set("n64_not64", not64);
    return t;
}
}  // namespace

TEST_CASE("saveQueryFor: Wii-Spiel auf echtem NAND") {
    auto q = saveQueryFor(game(Console::Wii, "RMGP01", "wbfs/x.wbfs"), wiiTarget(false), "usb:", table());
    CHECK(q.has_value());
    if (!q) return;
    CHECK(q->kind == SaveKind::WiiDisc);
    CHECK(q->id4 == "RMGP");
    CHECK(q->root == "nand:");
}

TEST_CASE("saveQueryFor: Wii-Spiel mit save=emu") {
    auto q = saveQueryFor(game(Console::Wii, "RMGP01", "wbfs/x.wbfs"), wiiTarget(true), "usb:", table());
    CHECK(q.has_value());
    if (q) CHECK(q->root == "usb:/emunand");
}

TEST_CASE("saveQueryFor: EmuNAND-Kanal mit emuNandFull") {
    LaunchTarget t = wiiTarget(true, true);
    t.channelPlanned = true;
    t.wii->channelTitleId = 0x00010001'4A414145ull;
    auto q = saveQueryFor(game(Console::Channel, "JAAE", nullptr, kNoDevice), t, "", table());
    CHECK(q.has_value());
    if (!q) return;
    CHECK(q->kind == SaveKind::Channel);
    CHECK(q->titleId == 0x00010001'4A414145ull);
    CHECK(q->root == "usb:/emunand");
}

TEST_CASE("saveQueryFor: echter NAND-Kanal (VC)") {
    LaunchTarget t;
    t.kind = LaunchTarget::Kind::Channel;
    t.titleId = 0x00010001'4E414C45ull;
    auto q = saveQueryFor(game(Console::N64, "n64/x.z64", "roms/n64/x.z64"), t, "sd:", table());
    CHECK(q.has_value());
    if (!q) return;
    CHECK(q->kind == SaveKind::Channel);
    CHECK(q->titleId == 0x00010001'4E414C45ull);
    CHECK(q->root == "nand:");
}

TEST_CASE("saveQueryFor: EmuNAND-Kanal ohne Plan hat noch keine Abfrage") {
    LaunchTarget t;
    t.kind = LaunchTarget::Kind::EmuChannel;
    CHECK(!saveQueryFor(game(Console::Channel, "JAAE", nullptr, kNoDevice), t, "", table()).has_value());
}

TEST_CASE("saveQueryFor: GameCube je Spiel, Multikarte, JP, echte Karte, Disc") {
    const Game g = game(Console::Gc, "GALE01", "games/x.iso");
    auto q = saveQueryFor(g, gcTarget(false, 0), "usb:", table());
    CHECK(q.has_value());
    if (q) {
        CHECK(q->kind == SaveKind::GcSingle);
        CHECK(q->id4 == "GALE");
        CHECK(q->root == "usb:");
    }
    q = saveQueryFor(g, gcTarget(false, kNinCfgMcMulti), "usb:", table());
    CHECK(q.has_value());
    if (q) {
        CHECK(q->kind == SaveKind::GcMulti);
        CHECK(!q->jp);
    }
    q = saveQueryFor(game(Console::Gc, "GALJ01", "games/x.iso"), gcTarget(false, kNinCfgMcMulti), "usb:", table());
    CHECK(q.has_value());
    if (q) CHECK(q->jp);
    CHECK(!saveQueryFor(g, gcTarget(true, 0), "usb:", table()).has_value());
    CHECK(!saveQueryFor(game(Console::Gc, "GALE01", nullptr, kNoDevice), gcTarget(false, 0), "", table()).has_value());
}

TEST_CASE("saveQueryFor: ROM über Emulator-Eintrag") {
    LaunchTarget t;
    t.kind = LaunchTarget::Kind::Dol;
    auto q = saveQueryFor(game(Console::Snes, "snes/Mario World (USA).sfc", "roms/snes/Mario World (USA).sfc"), t, "sd:",
                          table());
    CHECK(q.has_value());
    if (!q) return;
    CHECK(q->kind == SaveKind::Rom);
    CHECK(q->console == Console::Snes);
    CHECK(q->profile == "snes9xgx");
    CHECK(q->stem == "Mario World (USA)");
    CHECK(q->gameKey == "snes/Mario World (USA).sfc");
}

TEST_CASE("saveQueryFor: N64 über Not64 mit save_dir") {
    LaunchTarget t;
    t.kind = LaunchTarget::Kind::Dol;
    t.n64 = N64Method::Not64;
    auto q = saveQueryFor(game(Console::N64, "n64/Mario64.z64", "roms/n64/Mario64.z64"), t, "sd:", table());
    CHECK(q.has_value());
    if (!q) return;
    CHECK(q->profile == "not64");
    CHECK(q->stem == "Mario64");
    CHECK(q->saveDir == "sd:/not64/saves");
    CHECK(q->saveExt == std::vector<std::string>{".eep"});
}
