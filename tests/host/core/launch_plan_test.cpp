#include "core/launch_args.hpp"
#include "core/launch_layout.hpp"
#include "core/launch_plan.hpp"
#include "core/cios_scan.hpp"
#include "core/index_file.hpp"
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

TEST_CASE("preflightGame: N64 über Not64 baut argv mit rompath=\"{path}\"") {
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
    CHECK(r.value().dol.args[1] == std::string("rompath=\"") + kSm64 + "\"");
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
    REQUIRE(wii.isOk());  // M9: Wii geht nicht mehr über preflightEmulator, sondern über preflightWii
    CHECK(wii.value().kind == LaunchTarget::Kind::Wii);
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

namespace {
Game wiiGame() { return Game{Console::Wii, "RMCP01", "Mario Kart Wii", 0, 0, nullptr, "wbfs/Mario Kart Wii [RMCP01]/RMCP01.wbfs", 0, 0}; }
const CiosStatus kD2x{CiosState::D2x, 56, "v10"};
Result<FragList, LaunchError> goodFrags() { return Result<FragList, LaunchError>::ok(FragList{100, {{0, 2048, 100}}}); }
}  // namespace

TEST_CASE("preflightWii: Wii fehlt cIOS") {
    auto r = preflightWii(wiiGame(), "usb:", CiosStatus{CiosState::Missing, 56, ""}, goodFrags(), false);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().kind == LaunchFailure::CiosMissing);
    CHECK(r.error().context == "249");  // Dialog nennt den Slot
    CHECK(std::string(launchFailureName(LaunchFailure::CiosMissing)) == "CiosMissing");
}

TEST_CASE("preflightWii: festgelegter Slot fehlt → CiosMissing mit Slot, kein Rückfall") {
    const std::vector<CiosEntry> found{CiosEntry{249, CiosKind::D2x, 56, 21, "d2x", "v10"}};
    const CiosStatus st = ciosStatusFor(found, chooseCios(found, u8{222}, std::nullopt));
    auto r = preflightWii(wiiGame(), "usb:", st, goodFrags(), false);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().kind == LaunchFailure::CiosMissing);
    CHECK(r.error().context == "222");
}

TEST_CASE("preflightWii: Dry-Run trotz fehlendem cIOS") {
    auto r = preflightWii(wiiGame(), "usb:", CiosStatus{CiosState::Missing, 56, ""}, goodFrags(), true);
    REQUIRE(r.isOk());
    CHECK(r.value().cios.state == CiosState::Missing);
    CHECK(r.value().iosSlot == kDefaultCiosSlot);
}

TEST_CASE("preflightWii: Plan trägt den gewählten Slot") {
    const std::vector<CiosEntry> found{CiosEntry{249, CiosKind::Stub, 56, 0xFF00, "", ""},
                                       CiosEntry{250, CiosKind::D2x, 57, 21, "d2x", "v11"}};
    auto r = preflightWii(wiiGame(), "usb:", ciosStatusFor(found, chooseCios(found, std::nullopt, std::nullopt)),
                          goodFrags(), false);
    REQUIRE(r.isOk());
    CHECK(r.value().iosSlot == 250);
    CHECK(r.value().cios.base == 57);
}

TEST_CASE("preflightWii: unbestätigtes cIOS startet") {
    CHECK(preflightWii(wiiGame(), "usb:", CiosStatus{CiosState::Unverified, 56, ""}, goodFrags(), false).isOk());
}

TEST_CASE("preflightWii: Cheats (M15) – GCT und Hook im Plan, ohne Cheats hooktype 0") {
    WiiOptions o;
    o.cheats = true;
    o.hookType = 3;
    const std::vector<u8> gct(24, 0xAB);
    auto r = preflightWii(wiiGame(), "usb:", CiosStatus{CiosState::D2x, 56, "v10"}, goodFrags(), false, o, gct);
    REQUIRE(r.isOk());
    CHECK(r.value().gct == gct);
    CHECK(r.value().hookType == 3);

    auto none = preflightWii(wiiGame(), "usb:", CiosStatus{CiosState::D2x, 56, "v10"}, goodFrags(), false, o);
    REQUIRE(none.isOk());
    CHECK(none.value().gct.empty());
    CHECK(none.value().hookType == 0);  // keine gewählten Codes → kein Hook
}

TEST_CASE("preflightWii: Plan trägt die WIP-Einträge, unabhängig von Cheats") {
    const std::vector<WipEntry> wip = {{0x1000, 1, 2}, {0x2001, 3, 4}, {0x3000, 5, 6}};
    auto r = preflightWii(wiiGame(), "usb:", CiosStatus{CiosState::D2x, 249, "v11"}, goodFrags(), false, {}, {}, wip);
    REQUIRE(r.isOk());
    REQUIRE(r.value().wip.size() == 3);
    CHECK(r.value().wip[1].offset == 0x2001u);
    CHECK(r.value().wip[2].replacement == 6u);
    CHECK(r.value().hookType == 0);  // WIP braucht keinen Codehandler
    auto none = preflightWii(wiiGame(), "usb:", CiosStatus{CiosState::D2x, 249, "v11"}, goodFrags(), false);
    REQUIRE(none.isOk());
    CHECK(none.value().wip.empty());
}

TEST_CASE("preflightWii: GCT über der Codelisten-Grenze → CheatsTooLarge mit Größe") {
    WiiOptions o;
    o.cheats = true;
    CHECK(kWiiCodelistBytes == 0x80003000u - 0x80002338u);  // Codelistenbereich hinter dem Codehandler
    const std::vector<u8> fits(kWiiCodelistBytes, 0);
    CHECK(preflightWii(wiiGame(), "usb:", CiosStatus{CiosState::D2x, 56, "v10"}, goodFrags(), false, o, fits).isOk());
    const std::vector<u8> big(kWiiCodelistBytes + 8, 0);
    auto r = preflightWii(wiiGame(), "usb:", CiosStatus{CiosState::D2x, 56, "v10"}, goodFrags(), true, o, big);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().kind == LaunchFailure::CheatsTooLarge);
    CHECK(r.error().context == std::to_string(kWiiCodelistBytes + 8));
    CHECK(std::string(launchFailureName(LaunchFailure::CheatsTooLarge)) == "CheatsTooLarge");
}

TEST_CASE("preflightWii: cIOS-Fehler vor Fragmentfehler") {
    auto frags = Result<FragList, LaunchError>::err(fromFatError(FatError::NotFat32, "usb:"));
    auto r = preflightWii(wiiGame(), "usb:", CiosStatus{CiosState::Missing, 56, ""}, frags, false);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().kind == LaunchFailure::CiosMissing);
}

TEST_CASE("preflightWii: NTFS-Fehler wird WiiFsUnsupported") {
    for (FatError e : {FatError::SectorSize, FatError::NoFatVolume, FatError::NotFat32}) {
        CHECK(fromFatError(e, "NTFS").kind == LaunchFailure::WiiFsUnsupported);
        CHECK(fromFatError(e, "NTFS").context == "NTFS");
    }
    for (FatError e : {FatError::Io, FatError::BadChain, FatError::ChainTooShort})
        CHECK(fromFatError(e, "x").kind == LaunchFailure::WiiFileBroken);
    for (FragError e : {FragError::WbfsInvalid, FragError::TooFragmented, FragError::OutOfFile})
        CHECK(fromFragError(e, "x").kind == LaunchFailure::WiiFileBroken);
    auto r = preflightWii(wiiGame(), "usb:", kD2x,
                          Result<FragList, LaunchError>::err(fromFatError(FatError::NoFatVolume, "NTFS")), false);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().kind == LaunchFailure::WiiFsUnsupported);
    CHECK(std::string(launchFailureName(LaunchFailure::WiiFsUnsupported)) == "WiiFsUnsupported");
    CHECK(std::string(launchFailureName(LaunchFailure::WiiFileBroken)) == "WiiFileBroken");
}

TEST_CASE("fromExtentError: NTFS komprimiert/sparse/resident → FileNotContiguousReadable (M16)") {
    CHECK(fromExtentError(ExtentError::Unsupported, "usb2:/wii/a.iso").kind == LaunchFailure::FileNotContiguousReadable);
    CHECK(fromExtentError(ExtentError::Unsupported, "usb2:/wii/a.iso").context == "usb2:/wii/a.iso");
    CHECK(fromExtentError(ExtentError::Gap, "x").kind == LaunchFailure::WiiFileBroken);
    CHECK(fromExtentError(ExtentError::Overflow, "x").kind == LaunchFailure::WiiFileBroken);
    CHECK(std::string(launchFailureName(LaunchFailure::FileNotContiguousReadable)) == "FileNotContiguousReadable");
}

TEST_CASE("preflightWii: Split-Teil fehlt wird RomMissing") {
    const LaunchError e = fromFragError(FragError::PartMissing, "usb:/wbfs/X [RMCP01]/RMCP01.wbf1");
    CHECK(e.kind == LaunchFailure::RomMissing);
    CHECK(e.context == "usb:/wbfs/X [RMCP01]/RMCP01.wbf1");
}

TEST_CASE("preflightWii: Plan enthält ID6 und usb nach libRoot") {
    auto usb = preflightWii(wiiGame(), "usb:", kD2x, goodFrags(), false);
    REQUIRE(usb.isOk());
    CHECK(usb.value().id6 == "RMCP01");
    CHECK(usb.value().usb);
    CHECK(usb.value().cios.base == 56);
    CHECK(usb.value().fragBytes == serializeFragList(FragList{100, {{0, 2048, 100}}}));
    auto sd = preflightWii(wiiGame(), "sd:", kD2x, goodFrags(), false);
    REQUIRE(sd.isOk());
    CHECK_FALSE(sd.value().usb);
}

TEST_CASE("preflightWii: Schlüssel ohne ID6") {
    Game g = wiiGame();
    g.key = "RMC";
    auto r = preflightWii(g, "usb:", kD2x, goodFrags(), false);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().kind == LaunchFailure::Unsupported);
}

TEST_CASE("preflightGame: Wii liefert Kind::Wii ohne Plan") {
    test::MemFileSystem fs;
    auto r = preflightGame(fs, wiiGame(), "usb:", emus(), nullptr, true);
    REQUIRE(r.isOk());
    CHECK(r.value().kind == LaunchTarget::Kind::Wii);
    CHECK_FALSE(r.value().wii.has_value());
}

TEST_CASE("preflightGame: GC-Formate laut Nintendont (IsSupportedFileExt, FST)") {
    for (const char* rel : {"games/A.gcm", "games/B.ciso", "games/C.cso", "gc/D.nkit.iso"}) {
        CAPTURE(rel);
        auto fs = gcFs("sd:", "$$Version:6.509", rel);
        auto r = preflightGame(fs, gcGame(rel), "sd:", emus(), nullptr, true);
        CHECK(r.isOk());
    }
    for (const char* rel : {"games/E.gcz", "games/F.rvz", "games/G.nkit.gcz"}) {
        CAPTURE(rel);
        auto fs = gcFs("sd:", "$$Version:6.509", rel);
        const LaunchError e = gcErr(fs, gcGame(rel));
        CHECK(e.kind == LaunchFailure::NinFormat);
        CHECK(e.context == std::string(rel).substr(std::string(rel).rfind('.')));
    }
    CHECK(std::string(launchFailureName(LaunchFailure::NinFormat)) == "NinFormat");
}

TEST_CASE("preflightGame: GC-FST-Ordner bekommt den Pfad mit Schrägstrich") {
    test::MemFileSystem fs;
    fs.addFile(kNin, ninDol());
    fs.addFile("sd:/games/Zelda v1.0 [GZLE01]/sys/boot.bin", std::vector<u8>(64, 0));
    auto r = preflightGame(fs, gcGame("games/Zelda v1.0 [GZLE01]"), "sd:", emus(), nullptr, true);
    REQUIRE(r.isOk());
    CHECK(blobPath(r.value().dol.args[1]) == "/games/Zelda v1.0 [GZLE01]/");

    test::MemFileSystem empty;  // Ordner ohne sys/boot.bin: fehlt
    empty.addFile(kNin, ninDol());
    empty.addDir("sd:/games/Leer [GZLE01]");
    CHECK(gcErr(empty, gcGame("games/Leer [GZLE01]")).kind == LaunchFailure::RomMissing);
}

// --- M12-Review: Spiele auf weiteren USB-Partitionen (usb2 … usb8) ---
TEST_CASE("preflightWii: Spiel auf usb2 bootet vom USB-Gerät") {
    auto r = preflightWii(wiiGame(), "usb2:", kD2x, goodFrags(), false);
    REQUIRE(r.isOk());
    CHECK(r.value().usb);  // sonst liest das cIOS die USB-LBAs von der SD
}

TEST_CASE("preflightGame: GC auf usb2 → DeviceUnsupported, Nintendont kennt nur SD und die erste USB-Partition") {
    auto fs = gcFs("usb2:");
    const LaunchError e = gcErr(fs, gcGame(), true, "usb2:");
    CHECK(e.kind == LaunchFailure::DeviceUnsupported);
    CHECK(e.context == "USB 2");
}

TEST_CASE("preflightGame: ROM auf usb3 → DeviceUnsupported, die GX-Emulatoren hängen nur sd: und usb: ein") {
    auto fs = fsWith(goodDol());
    auto r = preflightGame(fs, nesGame(), "usb3:", emus(), nullptr, true);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().kind == LaunchFailure::DeviceUnsupported);
    CHECK(r.error().context == "USB 3");
    CHECK(std::string(launchFailureName(LaunchFailure::DeviceUnsupported)) == "DeviceUnsupported");
}

// --- M14: Start mit Spieloptionen ---
#include "core/game_options.hpp"

TEST_CASE("preflightGame: GC mit [gc] force_wide = 1 setzt das Bit in NIN_CFG") {
    auto fs = gcFs();
    const IniDoc config = IniDoc::parse("[gc]\nforce_wide = 1\n");
    const IniDoc games;
    const GameOptionInputs opts{config, games, true};
    auto r = preflightGame(fs, gcGame(), "sd:", emus(), nullptr, true, &opts);
    REQUIRE(r.isOk());
    CHECK(static_cast<u8>(r.value().dol.args[1][0x00B]) == (0x88 | 0x20));
    REQUIRE(r.value().gc.has_value());
    CHECK(r.value().gc->nin.extraFlags == kNinCfgForceWide);
    CHECK(r.value().warnings.empty());
}

TEST_CASE("preflightGame: GC-Cheats (M15) – Flags, CheatPath und GCT auf dem Spielgerät") {
    const IniDoc config = IniDoc::parse("[gc]\ncheats = 1\n");
    const IniDoc games;
    const std::vector<u8> gct(24, 0x11);
    GameOptionInputs opts{config, games, true};
    opts.gcCheats = gct;
    for (const char* root : {"sd:", "usb:"}) {
        auto fs = gcFs(root);
        auto r = preflightGame(fs, gcGame(), root, emus(), nullptr, true, &opts);
        REQUIRE(r.isOk());
        const std::string& blob = r.value().dol.args[1];
        CHECK((static_cast<u8>(blob[0x00B]) & 0x11) == 0x11);  // CHEATS | CHEAT_PATH
        CHECK(std::string(blob.c_str() + 0x113) == "/newlauncher/cache/GM8P01.gct");
        CHECK(r.value().gct == gct);
        CHECK(r.value().gctPath == std::string(root) + "/newlauncher/cache/GM8P01.gct");
    }
}

TEST_CASE("preflightGame: GC-Cheats nur mit Option und Codes") {
    auto fs = gcFs();
    const IniDoc on = IniDoc::parse("[gc]\ncheats = 1\n");
    const IniDoc off;
    const std::vector<u8> gct(24, 0x11);
    GameOptionInputs noCodes{on, off, true};
    GameOptionInputs noOption{off, off, true};
    noOption.gcCheats = gct;
    for (const GameOptionInputs* o : {&noCodes, &noOption}) {
        auto r = preflightGame(fs, gcGame(), "sd:", emus(), nullptr, true, o);
        REQUIRE(r.isOk());
        CHECK((static_cast<u8>(r.value().dol.args[1][0x00B]) & 0x11) == 0);
        CHECK(r.value().gct.empty());
        CHECK(r.value().gctPath.empty());
    }
}

TEST_CASE("preflightGame: GC-GCT über der Nintendont-Grenze → CheatsTooLarge mit Größe") {
    auto fs = gcFs();
    const IniDoc config = IniDoc::parse("[gc]\ncheats = 1\n");
    const IniDoc games;
    const std::vector<u8> fits(kNinCheatBytes, 0);
    const std::vector<u8> big(kNinCheatBytes + 8, 0);
    GameOptionInputs opts{config, games, true};
    opts.gcCheats = fits;
    CHECK(preflightGame(fs, gcGame(), "sd:", emus(), nullptr, true, &opts).isOk());
    opts.gcCheats = big;
    auto r = preflightGame(fs, gcGame(), "sd:", emus(), nullptr, true, &opts);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().kind == LaunchFailure::CheatsTooLarge);
    CHECK(r.error().context == std::to_string(big.size()));
}

TEST_CASE("preflightGame: GC ohne Optionen bleibt beim M8-Block") {
    auto fs = gcFs();
    const IniDoc empty;
    const GameOptionInputs opts{empty, empty, true};
    auto with = preflightGame(fs, gcGame(), "sd:", emus(), nullptr, true, &opts);
    auto without = preflightGame(fs, gcGame(), "sd:", emus(), nullptr, true);
    REQUIRE(with.isOk());
    REQUIRE(without.isOk());
    CHECK(with.value().dol.args[1] == without.value().dol.args[1]);
}

TEST_CASE("preflightGame: memcard = real ohne GC-Ports warnt, startet trotzdem") {
    auto fs = gcFs();
    const IniDoc config = IniDoc::parse("[gc]\nmemcard = real\nnative_si = 1\n");
    const IniDoc games;
    const GameOptionInputs opts{config, games, false};
    auto r = preflightGame(fs, gcGame(), "sd:", emus(), nullptr, true, &opts);
    REQUIRE(r.isOk());
    REQUIRE(r.value().warnings.size() == 2);
    CHECK(r.value().warnings[0] == LaunchWarning::MemcardRealNoPorts);
    CHECK(r.value().warnings[1] == LaunchWarning::NativeSiNoPorts);
    CHECK((static_cast<u8>(r.value().dol.args[1][0x00B]) & 0x08) == 0);  // kein MEMCARDEMU
    const GameOptionInputs ports{config, games, true};
    auto ok = preflightGame(fs, gcGame(), "sd:", emus(), nullptr, true, &ports);
    REQUIRE(ok.isOk());
    CHECK(ok.value().warnings.empty());
}

TEST_CASE("preflightWii: [game] ios = 250 wählt 250 und der Plan trägt die Optionen") {
    const std::vector<CiosEntry> found{CiosEntry{249, CiosKind::D2x, 56, 21, "d2x", "v11"},
                                       CiosEntry{250, CiosKind::D2x, 57, 21, "d2x", "v11"}};
    const Game g = wiiGame();
    const std::string id6 = std::string(g.key).substr(0, 6);
    const WiiOptions o = resolveWiiOptions(IniDoc{}, IniDoc::parse("[game " + id6 + "]\nios = 250\nvideo = pal60\n"), id6);
    auto r = preflightWii(g, "usb:", ciosStatusFor(found, chooseCios(found, o.ios, std::nullopt)), goodFrags(), false, o);
    REQUIRE(r.isOk());
    CHECK(r.value().iosSlot == 250);
    CHECK(r.value().options.cfg.vidMode == 3);
}

TEST_CASE("preflightWii: return_to nur mit d2x") {
    WiiOptions o;
    o.cfg.returnTo = 0x48415858;
    auto d2x = preflightWii(wiiGame(), "usb:", kD2x, goodFrags(), false, o);
    REQUIRE(d2x.isOk());
    CHECK(d2x.value().options.cfg.returnTo == 0x48415858u);
    auto other = preflightWii(wiiGame(), "usb:", CiosStatus{CiosState::Unverified, 56, ""}, goodFrags(), false, o);
    REQUIRE(other.isOk());
    CHECK(other.value().options.cfg.returnTo == 0u);
}

// --- M16: Preflight je Dateisystem ---
namespace {
test::MemFileSystem usbNesFs() {
    test::MemFileSystem fs;
    fs.addFile(kDol, goodDol());
    fs.addFile("usb:/roms/nes/Zelda (Europe) (Rev 1).nes", {});
    return fs;
}
}  // namespace

TEST_CASE("preflightGame: GC auf NTFS → NintendontNeedsFat, noch vor der Geräteprüfung") {
    auto fs = gcFs("usb2:");
    auto r = preflightGame(fs, gcGame(), "usb2:", emus(), nullptr, true, nullptr, FsKind::Ntfs);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().kind == LaunchFailure::NintendontNeedsFat);
    CHECK(r.error().context == "USB 2");
    CHECK(std::string(launchFailureName(LaunchFailure::NintendontNeedsFat)) == "NintendontNeedsFat");
    auto first = gcFs("usb:");
    auto onUsb = preflightGame(first, gcGame(), "usb:", emus(), nullptr, true, nullptr, FsKind::Ntfs);
    REQUIRE_FALSE(onUsb.isOk());
    CHECK(onUsb.error().kind == LaunchFailure::NintendontNeedsFat);
    CHECK(onUsb.error().context == "USB 1");
}

TEST_CASE("preflightGame: ROM auf ext mit Standardprofil → Warnung EmulatorFs, Start bleibt möglich") {
    auto fs = usbNesFs();
    auto r = preflightGame(fs, nesGame(), "usb:", emus(), nullptr, true, nullptr, FsKind::Ext);
    REQUIRE(r.isOk());
    REQUIRE(r.value().warnings.size() == 1);
    CHECK(r.value().warnings[0] == LaunchWarning::EmulatorFs);
    CHECK(r.value().emuName == "fceugx");
    CHECK(std::string(launchWarningName(LaunchWarning::EmulatorFs)) == "emulator_fs");
    auto fat = preflightGame(fs, nesGame(), "usb:", emus(), nullptr, true, nullptr, FsKind::Fat);
    REQUIRE(fat.isOk());
    CHECK(fat.value().warnings.empty());
}

TEST_CASE("preflightGame: emulators.ini fs = fat,ntfs → keine Warnung auf NTFS, auf ext weiter") {
    auto fs = usbNesFs();
    const EmulatorTable ini = EmulatorTable::fromIni(IniDoc::parse("[nes]\nfs = fat, NTFS\n"));
    auto r = preflightGame(fs, nesGame(), "usb:", ini, nullptr, true, nullptr, FsKind::Ntfs);
    REQUIRE(r.isOk());
    CHECK(r.value().warnings.empty());
    auto ext = preflightGame(fs, nesGame(), "usb:", ini, nullptr, true, nullptr, FsKind::Ext);
    REQUIRE(ext.isOk());
    CHECK(ext.value().warnings.size() == 1);
}

TEST_CASE("preflightGame: Wii auf ext oder NTFS → kein Fehler, kein Hinweis") {
    test::MemFileSystem fs;
    for (FsKind k : {FsKind::Ext, FsKind::Ntfs, FsKind::Wbfs}) {
        auto r = preflightGame(fs, wiiGame(), "usb2:", emus(), nullptr, true, nullptr, k);
        REQUIRE(r.isOk());
        CHECK(r.value().kind == LaunchTarget::Kind::Wii);
        CHECK(r.value().warnings.empty());
    }
}

TEST_CASE("preflightWiiDisc: gültige Wii-Disc erzeugt Plan ohne Fragmente mit Optionen") {
    Game g = wiiGame();
    g.path = "disc:";
    g.device = kNoDevice;
    g.flags = kFlagDisc;
    const OpticalDisc disc{DiscKind::Wii, "RMCP01", "Mario Kart Wii"};
    WiiOptions opts;
    opts.cfg.aspectRatio = 1;
    opts.cheats = true;
    opts.hookType = 2;
    const std::vector<u8> gct(16, 0x11);
    auto r = preflightWiiDisc(g, disc, kD2x, false, opts, gct);
    REQUIRE(r.isOk());
    CHECK(r.value().source == WiiBootSource::Disc);
    CHECK(r.value().id6 == "RMCP01");
    CHECK_FALSE(r.value().usb);
    CHECK(r.value().fragBytes.empty());
    CHECK(r.value().iosSlot == kD2x.slot);
    CHECK(r.value().options.cfg.aspectRatio == 1);
    CHECK(r.value().gct == gct);
    CHECK(r.value().hookType == 2);
}

TEST_CASE("preflightWiiDisc: andere Disc oder GC-Disc → DiscChanged, leer/unlesbar → DiscUnreadable") {
    Game g = wiiGame();
    g.path = "disc:";
    g.device = kNoDevice;
    g.flags = kFlagDisc;

    auto swapped = preflightWiiDisc(g, OpticalDisc{DiscKind::Wii, "RSBP01", "Brawl"}, kD2x, true);
    REQUIRE_FALSE(swapped.isOk());
    CHECK(swapped.error().kind == LaunchFailure::DiscChanged);
    CHECK(swapped.error().context == "RSBP01");
    CHECK(std::string(launchFailureName(LaunchFailure::DiscChanged)) == "DiscChanged");

    auto gc = preflightWiiDisc(g, OpticalDisc{DiscKind::Gc, "GM8P01", "Prime"}, kD2x, true);
    REQUIRE_FALSE(gc.isOk());
    CHECK(gc.error().kind == LaunchFailure::DiscChanged);
    CHECK(gc.error().context == "GM8P01");

    auto empty = preflightWiiDisc(g, std::nullopt, kD2x, true);
    REQUIRE_FALSE(empty.isOk());
    CHECK(empty.error().kind == LaunchFailure::DiscUnreadable);
    CHECK(std::string(launchFailureName(LaunchFailure::DiscUnreadable)) == "DiscUnreadable");

    auto unknown = preflightWiiDisc(g, OpticalDisc{DiscKind::Unknown, "", ""}, kD2x, true);
    REQUIRE_FALSE(unknown.isOk());
    CHECK(unknown.error().kind == LaunchFailure::DiscUnreadable);
}

TEST_CASE("preflightWiiDisc: fehlendes cIOS sperrt außer im Dry-Run, übergroße GCT → CheatsTooLarge") {
    Game g = wiiGame();
    g.flags = kFlagDisc;
    const OpticalDisc disc{DiscKind::Wii, "RMCP01", "Mario Kart Wii"};
    const CiosStatus missing{CiosState::Missing, 0, "", 249};
    auto fail = preflightWiiDisc(g, disc, missing, false);
    REQUIRE_FALSE(fail.isOk());
    CHECK(fail.error().kind == LaunchFailure::CiosMissing);
    CHECK(fail.error().context == "249");

    auto dry = preflightWiiDisc(g, disc, missing, true);
    REQUIRE(dry.isOk());
    CHECK(dry.value().source == WiiBootSource::Disc);

    const std::vector<u8> big(kWiiCodelistBytes + 8, 0);
    auto cheats = preflightWiiDisc(g, disc, kD2x, true, {}, big);
    REQUIRE_FALSE(cheats.isOk());
    CHECK(cheats.error().kind == LaunchFailure::CheatsTooLarge);
}

TEST_CASE("preflightGame: GC-Disc auf Wii erzeugt NIN_CFG mit GamePath=di und usb=0") {
    auto fs = gcFs();
    Game g{Console::Gc, "GM8P01", "Metroid Prime", 0, 0, nullptr, "disc:", kNoDevice, kFlagDisc};
    const IniDoc cfg = IniDoc::parse("[gc]\ncheats = 1\nforce_wide = 1\n");
    const IniDoc games;
    const std::vector<u8> gct(16, 0x55);
    GameOptionInputs opts{cfg, games, true, gct, Platform::Wii};
    auto r = preflightGame(fs, g, "", emus(), nullptr, true, &opts, FsKind::Unknown);
    REQUIRE(r.isOk());
    CHECK(r.value().kind == LaunchTarget::Kind::Dol);
    const std::string& nin = r.value().dol.args[1];
    CHECK(std::string(nin.c_str() + 0x14) == "di");
    CHECK((static_cast<u8>(nin[0x0A]) & 0x04) == 0);  // kNinCfgUsb aus
    CHECK((static_cast<u8>(nin[0x0B]) & 0x20) != 0);  // kNinCfgForceWide
    CHECK(r.value().gct == gct);
    CHECK(r.value().gctPath == "sd:/newlauncher/cache/GM8P01.gct");
}

TEST_CASE("preflightGame: GC-Disc auf vWii oder ohne GC-Ports → GcDiscUnsupported") {
    auto fs = gcFs();
    Game g{Console::Gc, "GM8P01", "Metroid Prime", 0, 0, nullptr, "disc:", kNoDevice, kFlagDisc};
    const IniDoc empty;
    const GameOptionInputs vWiiOpts{empty, empty, false, {}, Platform::VWii};
    auto vWiiRes = preflightGame(fs, g, "", emus(), nullptr, true, &vWiiOpts, FsKind::Unknown);
    REQUIRE_FALSE(vWiiRes.isOk());
    CHECK(vWiiRes.error().kind == LaunchFailure::GcDiscUnsupported);
    CHECK(std::string(launchFailureName(LaunchFailure::GcDiscUnsupported)) == "GcDiscUnsupported");

    const GameOptionInputs miniOpts{empty, empty, false, {}, Platform::Wii};
    auto miniRes = preflightGame(fs, g, "", emus(), nullptr, true, &miniOpts, FsKind::Unknown);
    REQUIRE_FALSE(miniRes.isOk());
    CHECK(miniRes.error().kind == LaunchFailure::GcDiscUnsupported);
}

TEST_CASE("preflightWii & preflightWiiDisc: save = emu prüft EmuNAND, FAT und d2x") {
    WiiOptions emuOpt;
    emuOpt.saveEmu = true;
    const EmuNandInfo none{};
    const EmuNandInfo ntfs{EmuNandStatus::NotFat, {"usb2", "/emunand"}, true, 1, "USB 2"};
    const EmuNandInfo okNand{EmuNandStatus::Ok, {"usb", "/emunand"}, true, 0, "USB 1"};

    auto rNo = preflightWii(wiiGame(), "usb:", kD2x, goodFrags(), false, emuOpt, {}, {}, &none);
    REQUIRE_FALSE(rNo.isOk());
    CHECK(rNo.error().kind == LaunchFailure::NoEmuNand);

    auto rFat = preflightWii(wiiGame(), "usb:", kD2x, goodFrags(), false, emuOpt, {}, {}, &ntfs);
    REQUIRE_FALSE(rFat.isOk());
    CHECK(rFat.error().kind == LaunchFailure::EmuNandNeedsFat);

    auto rD2x = preflightWii(wiiGame(), "usb:", CiosStatus{CiosState::Unverified, 56, "", 222}, goodFrags(), false,
                             emuOpt, {}, {}, &okNand);
    REQUIRE_FALSE(rD2x.isOk());
    CHECK(rD2x.error().kind == LaunchFailure::EmuNandNeedsD2x);

    auto rOk = preflightWii(wiiGame(), "usb:", kD2x, goodFrags(), false, emuOpt, {}, {}, &okNand);
    REQUIRE(rOk.isOk());
    REQUIRE(rOk.value().emuNand.has_value());
    CHECK_FALSE(rOk.value().emuNandFull);

    Game discG = wiiGame();
    discG.flags = kFlagDisc;
    auto dOk = preflightWiiDisc(discG, OpticalDisc{DiscKind::Wii, "RMCP01", "MKW"}, kD2x, false, emuOpt, {}, {}, &okNand);
    REQUIRE(dOk.isOk());
    REQUIRE(dOk.value().emuNand.has_value());
    CHECK_FALSE(dOk.value().emuNandFull);
}

TEST_CASE("preflightEmuChannel und preflightGame für Console::Channel") {
    test::MemFileSystem fs;
    const Game ch{Console::Channel, "000100014E4C5443", "Testkanal", 0, 0, nullptr,
                  "sd:/emunand/title/00010001/4e4c5443/content/00000000.app", kNoDevice, 0};
    auto tgt = preflightGame(fs, ch, "", emus(), nullptr, false);
    REQUIRE(tgt.isOk());
    CHECK(tgt.value().kind == LaunchTarget::Kind::EmuChannel);

    const EmuNandInfo okNand{EmuNandStatus::Ok, {"sd", "/emunand"}, false, 0, "SD"};
    auto plan = preflightEmuChannel(ch, kD2x, okNand, false);
    REQUIRE(plan.isOk());
    CHECK(plan.value().source == WiiBootSource::Channel);
    CHECK(plan.value().channelTitleId == 0x000100014E4C5443ull);
    REQUIRE(plan.value().emuNand.has_value());
    CHECK(plan.value().emuNandFull);
    CHECK(plan.value().fragBytes.empty());

    auto dryBadCios = preflightEmuChannel(ch, CiosStatus{CiosState::Unverified, 56, "", 222}, okNand, true);
    CHECK(dryBadCios.isOk());
}

TEST_CASE("applyEmuChannelPlan: Kanal-Plan wird Wii-Ziel und ist als geplant markiert") {
    test::MemFileSystem fs;
    const Game ch{Console::Channel, "000100014E4C5443", "Testkanal", 0, 0, nullptr,
                  "sd:/emunand/title/00010001/4e4c5443/content/00000000.app", kNoDevice, 0};
    auto tgt = preflightGame(fs, ch, "", emus(), nullptr, false);
    REQUIRE(tgt.isOk());
    CHECK_FALSE(tgt.value().channelPlanned);

    const EmuNandInfo okNand{EmuNandStatus::Ok, {"sd", "/emunand"}, false, 0, "SD"};
    auto plan = preflightEmuChannel(ch, kD2x, okNand, false);
    REQUIRE(plan.isOk());
    LaunchTarget& t = tgt.value();
    applyEmuChannelPlan(t, std::move(plan.value()));
    CHECK(t.kind == LaunchTarget::Kind::Wii);
    CHECK(t.channelPlanned);  // main.cpp darf danach kein prepareWii (USB-Plan) mehr rechnen
    REQUIRE(t.wii.has_value());
    CHECK(t.wii->source == WiiBootSource::Channel);
    CHECK(t.wii->channelTitleId == 0x000100014E4C5443ull);
}

TEST_CASE("preflightGame: vWii ohne GC-Port-Warnungen und mit wiiu_wide-Standard") {
    auto fs = gcFs();
    const IniDoc cfg = IniDoc::parse("[gc]\nmemcard = real\nnative_si = 1\n");
    const IniDoc games;
    const GameOptionInputs opts{cfg, games, false, {}, Platform::VWii};
    auto r = preflightGame(fs, gcGame(), "sd:", emus(), nullptr, true, &opts);
    REQUIRE(r.isOk());
    CHECK(r.value().warnings.empty());
    // wiiu_wide (1 << 15) gesetzt, NATIVE_SI (1 << 14) aus, MEMCARDEMU (1 << 3) an
    CHECK((static_cast<u8>(r.value().dol.args[1][0x00A]) & 0x80) != 0);
    CHECK((static_cast<u8>(r.value().dol.args[1][0x00A]) & 0x40) == 0);
    CHECK((static_cast<u8>(r.value().dol.args[1][0x00B]) & 0x08) != 0);
}

TEST_CASE("preflightDol (M20): eigene DOL mit Zusatzargumenten, z. B. Neustart über die Fernsteuerung") {
    const auto dol = goodDol();
    test::MemFileSystem fs;
    fs.addFile("sd:/apps/newlauncher/boot.dol", dol);
    const std::vector<std::string> extra{"--remote"};
    auto r = preflightDol(fs, "sd:/apps/newlauncher/boot.dol", extra);
    REQUIRE(r.isOk());
    CHECK(r.value().dolPath == "sd:/apps/newlauncher/boot.dol");
    CHECK(r.value().dolSize == dol.size());
    CHECK(r.value().args == std::vector<std::string>{"sd:/apps/newlauncher/boot.dol", "--remote"});
    CHECK_FALSE(r.value().argvBlock.empty());

    auto missing = preflightDol(fs, "sd:/apps/fehlt/boot.dol", {});
    REQUIRE_FALSE(missing.isOk());
    CHECK(missing.error().kind == LaunchFailure::EmuMissing);
}
