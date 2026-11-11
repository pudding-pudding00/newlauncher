#include "core/launch_args.hpp"
#include "core/launch_layout.hpp"
#include "core/launch_plan.hpp"
#include "support/dol_images.hpp"
#include "support/mem_fs.hpp"
#include <doctest/doctest.h>
#include <string>
using namespace nl;

namespace {
constexpr const char* kDol = "sd:/apps/fceugx/boot.dol";

std::vector<u8> goodDol() { return test::dolImage({{0x100, 0x80004000, 0x200}}, {}, 0x80004000); }

Game nesGame(const char* path = "roms/nes/Zelda (Europe) (Rev 1).nes", Console c = Console::Nes) {
    return Game{c, "nes/Zelda (Europe) (Rev 1).nes", "Zelda", 0, 0, nullptr, path, 0, 0};
}

test::MemFileSystem fsWith(std::vector<u8> dol) {
    test::MemFileSystem fs;
    fs.addFile(kDol, std::move(dol));
    fs.addFile("sd:/roms/nes/Zelda (Europe) (Rev 1).nes", {});
    return fs;
}

EmulatorTable emus() { return EmulatorTable::fromIni(IniDoc::parse("")); }
}  // namespace

TEST_CASE("gültiger Start liefert DOL-Pfad, Argumente und argv-Block") {
    const auto dol = goodDol();
    auto fs = fsWith(dol);
    auto r = preflightEmulator(fs, nesGame(), "sd:", emus());
    REQUIRE(r.isOk());
    CHECK(r.value().dolPath == kDol);
    CHECK(r.value().dolSize == dol.size());
    REQUIRE(r.value().args.size() == 3);
    CHECK(r.value().args[1] == "sd:/roms/nes/");
    CHECK(r.value().args[2] == "Zelda (Europe) (Rev 1).nes");
    CHECK_FALSE(r.value().argvBlock.empty());
}

TEST_CASE("DOL fehlt → EmuMissing mit DOL-Pfad") {
    test::MemFileSystem fs;
    fs.addFile("sd:/roms/nes/Zelda (Europe) (Rev 1).nes", {});
    auto r = preflightEmulator(fs, nesGame(), "sd:", emus());
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().kind == LaunchFailure::EmuMissing);
    CHECK(r.error().context == kDol);
}

TEST_CASE("DOL 0 Byte bzw. Müll → EmuInvalid") {
    auto empty = fsWith({});
    auto r = preflightEmulator(empty, nesGame(), "sd:", emus());
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().kind == LaunchFailure::EmuInvalid);
    CHECK(r.error().context == kDol);
    auto garbage = fsWith(std::vector<u8>(0x400, 0xAB));
    auto g = preflightEmulator(garbage, nesGame(), "sd:", emus());
    REQUIRE_FALSE(g.isOk());
    CHECK(g.error().kind == LaunchFailure::EmuInvalid);
}

TEST_CASE("Emulator-Datei ohne .dol-Endung: fehlt → EmuMissing, Müll → EmuInvalid, jeweils mit Pfad") {
    constexpr const char* kElf = "sd:/apps/snes9xgx/boot.elf";
    const EmulatorTable t = EmulatorTable::fromIni(IniDoc::parse(std::string("[nes]\ndol = ") + kElf + "\n"));
    test::MemFileSystem fs;
    fs.addFile("sd:/roms/nes/Zelda (Europe) (Rev 1).nes", {});
    auto missing = preflightEmulator(fs, nesGame(), "sd:", t);
    REQUIRE_FALSE(missing.isOk());
    CHECK(missing.error().kind == LaunchFailure::EmuMissing);
    CHECK(missing.error().context == kElf);
    fs.addFile(kElf, std::vector<u8>(0x400, 0xAB));
    auto invalid = preflightEmulator(fs, nesGame(), "sd:", t);
    REQUIRE_FALSE(invalid.isOk());
    CHECK(invalid.error().kind == LaunchFailure::EmuInvalid);
    CHECK(invalid.error().context == kElf);
}

TEST_CASE("DOL größer als kMaxDolBytes → EmuInvalid") {
    // Sektion endet weit hinter dem Header, Datei ist entsprechend groß; Größe kommt aus list().
    auto img = test::dolImage({{0x100, 0x80004000, 0x200}}, {}, 0x80004000);
    img.resize(kMaxDolBytes + 1, 0);
    auto fs = fsWith(std::move(img));
    auto r = preflightEmulator(fs, nesGame(), "sd:", emus());
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().kind == LaunchFailure::EmuInvalid);
    CHECK(r.error().context == kDol);
}

TEST_CASE("ROM fehlt → RomMissing mit ROM-Pfad") {
    test::MemFileSystem fs;
    fs.addFile(kDol, goodDol());
    auto r = preflightEmulator(fs, nesGame(), "sd:", emus());
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().kind == LaunchFailure::RomMissing);
    CHECK(r.error().context == "sd:/roms/nes/Zelda (Europe) (Rev 1).nes");
}

TEST_CASE("N64/GC/Wii → Unsupported mit Konsolenname") {
    auto fs = fsWith(goodDol());
    for (Console c : {Console::N64, Console::Gc, Console::Wii}) {
        auto r = preflightEmulator(fs, nesGame("roms/n64/x.z64", c), "sd:", emus());
        REQUIRE_FALSE(r.isOk());
        CHECK(r.error().kind == LaunchFailure::Unsupported);
        CHECK(r.error().context == consoleInfo(c).name);
    }
}

TEST_CASE("argv zu lang → ArgsTooLong") {
    auto fs = fsWith(goodDol());
    const EmulatorTable t = EmulatorTable::fromIni(IniDoc::parse("[nes]\nargs = " + std::string(kMaxArgvBytes, 'x') + "\n"));
    auto r = preflightEmulator(fs, nesGame(), "sd:", t);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().kind == LaunchFailure::ArgsTooLong);
}

TEST_CASE("launchFailureName nennt jede Fehlerart") {
    CHECK(std::string(launchFailureName(LaunchFailure::Unsupported)) == "Unsupported");
    CHECK(std::string(launchFailureName(LaunchFailure::EmuMissing)) == "EmuMissing");
    CHECK(std::string(launchFailureName(LaunchFailure::EmuInvalid)) == "EmuInvalid");
    CHECK(std::string(launchFailureName(LaunchFailure::RomMissing)) == "RomMissing");
    CHECK(std::string(launchFailureName(LaunchFailure::ArgsTooLong)) == "ArgsTooLong");
    CHECK(std::string(launchFailureName(LaunchFailure::NoMemory)) == "NoMemory");
}

TEST_CASE("Preflight schreibt nichts") {
    auto fs = fsWith(goodDol());
    REQUIRE(preflightEmulator(fs, nesGame(), "sd:", emus()).isOk());
    CHECK(fs.writes == 0);
}

namespace {
constexpr const char* kSm64 = "sd:/roms/n64/Super Mario 64 (Europe).z64";
constexpr u64 kVcId = 0x000100014E414D50ULL;
Game n64Game() {
    return Game{Console::N64, "n64/Super Mario 64 (Europe).z64", "Super Mario 64", 0, 0, nullptr, "roms/n64/Super Mario 64 (Europe).z64", 0, 0};
}
}  // namespace

TEST_CASE("preflightGame: N64 über Not64 baut argv mit {path}") {
    test::MemFileSystem fs;
    fs.addFile("sd:/apps/not64/boot.dol", goodDol());
    fs.addFile(kSm64, {});
    N64Inputs in;
    in.not64Dol = true;
    auto r = preflightGame(fs, n64Game(), "sd:", emus(), &in, true);
    REQUIRE(r.isOk());
    CHECK(r.value().kind == LaunchTarget::Kind::Dol);
    CHECK(r.value().n64 == N64Method::Not64);
    REQUIRE(r.value().dol.args.size() == 2);
    CHECK(r.value().dol.args[0] == "sd:/apps/not64/boot.dol");
    CHECK(r.value().dol.args[1] == kSm64);
}

TEST_CASE("preflightGame: N64 über VC liefert Kanalziel") {
    test::MemFileSystem fs;  // leer: weder DOL noch ROM nötig
    const std::vector<u64> installed{kVcId};
    N64Inputs in;
    in.mappedTitle = kVcId;
    in.installedTitles = installed;
    auto r = preflightGame(fs, n64Game(), "sd:", emus(), &in, true);
    REQUIRE(r.isOk());
    CHECK(r.value().kind == LaunchTarget::Kind::Channel);
    CHECK(r.value().titleId == kVcId);
    CHECK(r.value().n64 == N64Method::Vc);
}

TEST_CASE("preflightGame: N64 ohne Weg → NoN64Path") {
    test::MemFileSystem fs;
    N64Inputs in;
    auto r = preflightGame(fs, n64Game(), "sd:", emus(), &in, true);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().kind == LaunchFailure::NoN64Path);
    CHECK(r.error().context == consoleInfo(Console::N64).name);
    auto none = preflightGame(fs, n64Game(), "sd:", emus(), nullptr, true);
    REQUIRE_FALSE(none.isOk());
    CHECK(none.error().kind == LaunchFailure::NoN64Path);
    CHECK(std::string(launchFailureName(LaunchFailure::NoN64Path)) == "NoN64Path");
}

TEST_CASE("preflightGame: NES unverändert wie preflightEmulator") {
    auto fs = fsWith(goodDol());
    auto r = preflightGame(fs, nesGame(), "sd:", emus(), nullptr, true);
    REQUIRE(r.isOk());
    CHECK(r.value().kind == LaunchTarget::Kind::Dol);
    CHECK_FALSE(r.value().n64.has_value());
    CHECK(r.value().dol.dolPath == kDol);
    REQUIRE(r.value().dol.args.size() == 3);
    auto wii = preflightGame(fs, nesGame("games/x/game.iso", Console::Wii), "sd:", emus(), nullptr, true);
    REQUIRE_FALSE(wii.isOk());
    CHECK(wii.error().kind == LaunchFailure::Unsupported);
}

TEST_CASE("preflightGame: NES ignoriert ahbprotOff") {
    auto fs = fsWith(goodDol());
    CHECK(preflightGame(fs, nesGame(), "sd:", emus(), nullptr, false).isOk());
}

// --- M8: GameCube über Nintendont ---
namespace {
constexpr const char* kNin = "sd:/apps/nintendont/boot.dol";
constexpr const char* kGcRel = "games/Metroid Prime [GM8P01]/game.iso";

Game gcGame(const char* path = kGcRel) { return Game{Console::Gc, "GM8P01", "Metroid Prime", 0, 0, nullptr, path, 0, 0}; }

std::vector<u8> ninDol(std::string_view tag = "$$Version:6.509") {
    auto img = goodDol();
    for (std::size_t i = 0; i < tag.size(); ++i) img[0x140 + i] = static_cast<u8>(tag[i]);
    return img;
}

test::MemFileSystem gcFs(std::string_view root = "sd:", std::string_view tag = "$$Version:6.509", const char* rel = kGcRel) {
    test::MemFileSystem fs;
    fs.addFile(kNin, ninDol(tag));
    fs.addFile(std::string(root) + "/" + rel, std::vector<u8>(64, 0));
    return fs;
}

std::string blobPath(const std::string& blob) {
    std::string s;
    for (std::size_t i = 0x014; i < 0x113 && blob[i] != 0; ++i) s.push_back(blob[i]);
    return s;
}

LaunchError gcErr(test::MemFileSystem& fs, const Game& g, bool ahbprotOff = true, std::string_view root = "sd:") {
    auto r = preflightGame(fs, g, root, emus(), nullptr, ahbprotOff);
    REQUIRE_FALSE(r.isOk());
    return r.error();
}
}  // namespace

TEST_CASE("preflightGame: GC startet mit NIN_CFG als argv[1]") {
    auto fs = gcFs();
    auto r = preflightGame(fs, gcGame(), "sd:", emus(), nullptr, true);
    REQUIRE(r.isOk());
    const LaunchTarget& t = r.value();
    CHECK(t.kind == LaunchTarget::Kind::Dol);
    CHECK(t.dol.dolPath == kNin);
    REQUIRE(t.dol.args.size() == 2);
    CHECK(t.dol.args[0] == kNin);
    REQUIRE(t.dol.args[1].size() == 548);
    CHECK(blobPath(t.dol.args[1]) == "/games/Metroid Prime [GM8P01]/game.iso");
    CHECK(static_cast<u8>(t.dol.args[1][0x00A]) == 0x00);
    CHECK_FALSE(t.dol.argvBlock.empty());
    REQUIRE(t.nintendont.has_value());
    CHECK(*t.nintendont == NinVersion{6, 509});
}

TEST_CASE("preflightGame: GC von USB setzt USB-Bit") {
    auto fs = gcFs("usb:");
    auto r = preflightGame(fs, gcGame(), "usb:", emus(), nullptr, true);
    REQUIRE(r.isOk());
    CHECK(static_cast<u8>(r.value().dol.args[1][0x00A]) == 0x04);
    CHECK(blobPath(r.value().dol.args[1]) == "/games/Metroid Prime [GM8P01]/game.iso");
}

TEST_CASE("preflightGame: AHBPROT wird vor allem anderen geprüft") {
    test::MemFileSystem fs;  // nicht einmal die DOL
    const LaunchError e = gcErr(fs, gcGame(), false);
    CHECK(e.kind == LaunchFailure::AhbProt);
    CHECK(std::string(launchFailureName(LaunchFailure::AhbProt)) == "AhbProt");
}

TEST_CASE("preflightGame: GC-Fehler in der Reihenfolge des Plans") {
    SUBCASE("DOL fehlt") {
        test::MemFileSystem fs;
        const LaunchError e = gcErr(fs, gcGame());
        CHECK(e.kind == LaunchFailure::NinMissing);
        CHECK(e.context == kNin);
    }
    SUBCASE("DOL ungültig") {
        test::MemFileSystem fs;
        fs.addFile(kNin, std::vector<u8>(0x400, 0xAB));
        const LaunchError e = gcErr(fs, gcGame());
        CHECK(e.kind == LaunchFailure::EmuInvalid);
        CHECK(e.context == kNin);
    }
    SUBCASE("keine Version") {
        auto fs = gcFs("sd:", "");
        const LaunchError e = gcErr(fs, gcGame());
        CHECK(e.kind == LaunchFailure::NinUnknown);
        CHECK(e.context == kNin);
    }
    SUBCASE("zu alt") {
        auto fs = gcFs("sd:", "$$Version:6.480");
        const LaunchError e = gcErr(fs, gcGame());
        CHECK(e.kind == LaunchFailure::NinTooOld);
        CHECK(e.context == "6.480");
    }
    SUBCASE("Spieldatei fehlt") {
        test::MemFileSystem fs;
        fs.addFile(kNin, ninDol());
        const LaunchError e = gcErr(fs, gcGame());
        CHECK(e.kind == LaunchFailure::RomMissing);
        CHECK(e.context == std::string("sd:/") + kGcRel);
    }
    SUBCASE("Pfad zu lang") {
        const std::string rel = "games/" + std::string(240, 'x') + " [GM8P01]/game.iso";  // > 254 mit "/"
        auto fs = gcFs("sd:", "$$Version:6.509", rel.c_str());
        const LaunchError e = gcErr(fs, gcGame(rel.c_str()));
        CHECK(e.kind == LaunchFailure::NinPathTooLong);
        CHECK(e.context == "/" + rel);
    }
    CHECK(std::string(launchFailureName(LaunchFailure::NinMissing)) == "NinMissing");
    CHECK(std::string(launchFailureName(LaunchFailure::NinUnknown)) == "NinUnknown");
    CHECK(std::string(launchFailureName(LaunchFailure::NinTooOld)) == "NinTooOld");
    CHECK(std::string(launchFailureName(LaunchFailure::NinPathTooLong)) == "NinPathTooLong");
}

TEST_CASE("preflightGame: argvBlock passt für einen 254-Byte-Pfad") {
    const std::string rel = std::string(253 - 9, 'y') + "/game.iso";  // "/" + rel == 254 Byte
    REQUIRE(rel.size() + 1 == 254);
    auto fs = gcFs("sd:", "$$Version:6.509", rel.c_str());
    auto r = preflightGame(fs, gcGame(rel.c_str()), "sd:", emus(), nullptr, true);
    REQUIRE(r.isOk());
    CHECK(blobPath(r.value().dol.args[1]) == "/" + rel);
}

TEST_CASE("EmulatorTable::nintendont: Standard und aus der ini") {
    CHECK(emus().nintendont().dol == kNin);
    auto t = EmulatorTable::fromIni(IniDoc::parse("[nintendont]\ndol = sd:/apps/argv_echo/boot.dol\n"));
    CHECK(t.nintendont().dol == "sd:/apps/argv_echo/boot.dol");
}

TEST_CASE("fileSize") {
    test::MemFileSystem fs;
    fs.addFile("sd:/apps/x/boot.dol", std::vector<u8>(123, 0));
    CHECK(fileSize(fs, "sd:/apps/x/boot.dol") == 123u);
    CHECK_FALSE(fileSize(fs, "sd:/apps/x/fehlt.dol").has_value());
    CHECK_FALSE(fileSize(fs, "sd:/apps/fehlt/boot.dol").has_value());
}
