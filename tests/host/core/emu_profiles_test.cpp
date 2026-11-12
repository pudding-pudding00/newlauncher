#include "core/emu_profiles.hpp"
#include "core/ini.hpp"
#include "support/mem_fs.hpp"
#include <doctest/doctest.h>
using namespace nl;

namespace {

FoundApp app(std::string device, std::string folder, std::string dol = "boot.dol", std::string name = "") {
    return FoundApp{device, folder, device + ":/apps/" + folder + "/" + dol, std::move(name)};
}

std::vector<u8> bytes(std::string_view s) { return std::vector<u8>(s.begin(), s.end()); }

const EmulatorTable kNoIni = EmulatorTable::fromIni(IniDoc::parse(""));

// emulators.ini, wie v1 sie beim ersten Start immer vollständig schrieb
constexpr const char* kV1File =
    "[nes]\n"
    "dol = sd:/apps/fceugx/boot.dol\n"
    "args = {dir}|{file}\n"
    "[snes]\n"
    "dol = sd:/apps/snes9xgx/boot.dol\n"
    "args = {dir}|{file}\n"
    "[gb]\n"
    "dol = sd:/apps/vbagx/boot.dol\n"
    "args = {dir}|{file}\n"
    "[gbc]\n"
    "dol = sd:/apps/vbagx/boot.dol\n"
    "args = {dir}|{file}\n"
    "[gba]\n"
    "dol = sd:/apps/vbagx/boot.dol\n"
    "args = {dir}|{file}\n"
    "[n64_not64]\n"
    "dol = sd:/apps/not64/boot.dol\n"
    "args = {path}\n"
    "[n64_wii64]\n"
    "dol = sd:/apps/wii64/boot.dol\n"
    "args = {path}\n"
    "[nintendont]\n"
    "dol = sd:/apps/nintendont/boot.dol\n";

}  // namespace

TEST_CASE("isV1Default: alle acht v1-Abschnitte, nur der exakte Pfad") {
    CHECK(isV1Default("nes", "sd:/apps/fceugx/boot.dol"));
    CHECK(isV1Default("snes", "sd:/apps/snes9xgx/boot.dol"));
    CHECK(isV1Default("gb", "sd:/apps/vbagx/boot.dol"));
    CHECK(isV1Default("gbc", "sd:/apps/vbagx/boot.dol"));
    CHECK(isV1Default("gba", "sd:/apps/vbagx/boot.dol"));
    CHECK(isV1Default("n64_not64", "sd:/apps/not64/boot.dol"));
    CHECK(isV1Default("n64_wii64", "sd:/apps/wii64/boot.dol"));
    CHECK(isV1Default("nintendont", "sd:/apps/nintendont/boot.dol"));
    CHECK_FALSE(isV1Default("nes", "sd:/apps/argv_echo/boot.dol"));
    CHECK_FALSE(isV1Default("snes", "sd:/apps/fceugx/boot.dol"));
    CHECK_FALSE(isV1Default("n64", "sd:/apps/not64/boot.dol"));
}

TEST_CASE("profileOrder: GX-Emulator vor RetroArch") {
    const auto nes = profileOrder(Console::Nes);
    REQUIRE(nes.size() == 2);
    CHECK(std::string_view(nes[0]) == "fceugx");
    CHECK(std::string_view(nes[1]) == "retroarch-fceumm");
    CHECK(std::string_view(profileOrder(Console::Snes)[1]) == "retroarch-snes9x2010");
    CHECK(std::string_view(profileOrder(Console::Gbc)[1]) == "retroarch-gambatte");
    CHECK(std::string_view(profileOrder(Console::Gba)[1]) == "retroarch-mgba");
    CHECK(profileOrder(Console::Wii).empty());
}

TEST_CASE("resolveEmulator: eigener INI-Abschnitt schlägt Spielprofil und Erkennung") {
    const EmulatorTable ini = EmulatorTable::fromIni(IniDoc::parse("[nes]\ndol = sd:/apps/argv_echo/boot.dol\n"));
    const std::vector<FoundApp> found{app("sd", "fceugx"), app("sd", "retroarch-wii", "fceumm_libretro_wii.dol")};
    const auto r = resolveEmulator(Console::Nes, ini, found, std::string_view("retroarch-fceumm"));
    REQUIRE(r);
    CHECK(r->dol == "sd:/apps/argv_echo/boot.dol");
    CHECK(r->fromIni);
    CHECK(r->profile.empty());
    CHECK_FALSE(r->badProfile);
}

TEST_CASE("resolveEmulator: Spielprofil schlägt die Reihenfolge") {
    const std::vector<FoundApp> found{app("sd", "fceugx"), app("sd", "retroarch-wii", "fceumm_libretro_wii.dol")};
    const auto r = resolveEmulator(Console::Nes, kNoIni, found, std::string_view("retroarch-fceumm"));
    REQUIRE(r);
    CHECK(r->profile == "retroarch-fceumm");
    CHECK(r->dol == "sd:/apps/retroarch-wii/fceumm_libretro_wii.dol");
    CHECK(r->args == "{dir}|{file}");
    CHECK_FALSE(r->fromIni);
}

TEST_CASE("resolveEmulator: ohne Spielprofil gewinnt das erste installierte Profil") {
    const std::vector<FoundApp> found{app("sd", "retroarch-wii", "fceumm_libretro_wii.dol"), app("sd", "fceugx")};
    const auto r = resolveEmulator(Console::Nes, kNoIni, found, std::nullopt);
    REQUIRE(r);
    CHECK(r->profile == "fceugx");
    CHECK(r->dol == "sd:/apps/fceugx/boot.dol");
}

TEST_CASE("resolveEmulator: v1-Standard-INI bei fehlender Datei → Erkennung greift") {
    const EmulatorTable ini = EmulatorTable::fromIni(IniDoc::parse(defaultEmulatorsText() + "[nes]\ndol = sd:/apps/fceugx/boot.dol\n"));
    const std::vector<FoundApp> found{app("usb2", "fceugx")};
    const auto r = resolveEmulator(Console::Nes, ini, found, std::nullopt);
    REQUIRE(r);
    CHECK(r->dol == "usb2:/apps/fceugx/boot.dol");
    CHECK(r->profile == "fceugx");
    CHECK_FALSE(r->fromIni);
}

TEST_CASE("resolveEmulator: v1-Standard-INI, Datei vorhanden → bleibt, gilt als erkannt") {
    const EmulatorTable ini = EmulatorTable::fromIni(IniDoc::parse("[nes]\ndol = sd:/apps/fceugx/boot.dol\nargs = {dir}|{file}\n"));
    const std::vector<FoundApp> found{app("sd", "FCEUGX"), app("usb", "fceugx")};
    const auto r = resolveEmulator(Console::Nes, ini, found, std::nullopt);
    REQUIRE(r);
    CHECK(r->dol == "sd:/apps/fceugx/boot.dol");
    CHECK_FALSE(r->fromIni);  // v1 schrieb die Vorgabe immer; sie ist keine Nutzerentscheidung
    CHECK(r->profile == "fceugx");
}

TEST_CASE("resolveTable: komplette v1-emulators.ini, nichts installiert → kein Eintrag gilt als aus der INI") {
    const EmulatorTable ini = EmulatorTable::fromIni(IniDoc::parse(kV1File));
    const EmulatorTable t = resolveTable(ini, {});
    for (Console c : {Console::Nes, Console::Snes, Console::Gb, Console::Gbc, Console::Gba}) {
        REQUIRE(t.forConsole(c));
        CHECK_FALSE(t.forConsole(c)->fromFile);
    }
    CHECK_FALSE(t.forN64(N64Method::Not64)->fromFile);
    CHECK_FALSE(t.nintendont().fromFile);
}

TEST_CASE("resolveEmulator: eigener INI-Abschnitt auf einen RetroArch-Kern trägt dessen Profil (kein ZIP)") {
    const EmulatorTable ini = EmulatorTable::fromIni(
        IniDoc::parse("[nes]\ndol = sd:/apps/retroarch-wii/fceumm_libretro_wii.dol\nargs = {dir}|{file}\n"));
    const std::vector<FoundApp> found{app("sd", "retroarch-wii", "fceumm_libretro_wii.dol")};
    const auto r = resolveEmulator(Console::Nes, ini, found, std::nullopt);
    REQUIRE(r);
    CHECK(r->fromIni);
    CHECK(r->profile == "retroarch-fceumm");
    CHECK((zipConsoles(resolveTable(ini, found)) & (1u << static_cast<unsigned>(Console::Nes))) == 0);
}

TEST_CASE("resolveEmulator: nur RetroArch installiert → retroarch-fceumm für NES") {
    const std::vector<FoundApp> found{app("sd", "retroarch-wii", "fceumm_libretro_wii.dol"),
                                      app("sd", "retroarch-wii", "snes9x2010_libretro_wii.dol")};
    const auto r = resolveEmulator(Console::Nes, kNoIni, found, std::nullopt);
    REQUIRE(r);
    CHECK(r->profile == "retroarch-fceumm");
    CHECK(r->dol == "sd:/apps/retroarch-wii/fceumm_libretro_wii.dol");
}

TEST_CASE("resolveEmulator: unbekanntes oder fremdes Spielprofil → Reihenfolge und badProfile") {
    const std::vector<FoundApp> found{app("sd", "fceugx")};
    for (std::string_view bad : {std::string_view("zsnes"), std::string_view("snes9xgx")}) {
        const auto r = resolveEmulator(Console::Nes, kNoIni, found, bad);
        REQUIRE(r);
        CHECK(r->profile == "fceugx");
        CHECK(r->badProfile);
    }
}

TEST_CASE("resolveEmulator: nichts installiert → INI-Eintrag (v1-Standard) bleibt, Profil leer") {
    const auto r = resolveEmulator(Console::Snes, kNoIni, {}, std::nullopt);
    REQUIRE(r);
    CHECK(r->dol == "sd:/apps/snes9xgx/boot.dol");
    CHECK(r->profile.empty());
    CHECK_FALSE(r->fromIni);
    CHECK_FALSE(resolveEmulator(Console::Wii, kNoIni, {}, std::nullopt));
}

TEST_CASE("resolveEmulator: Ordner per meta.xml-Name erkannt") {
    const std::vector<FoundApp> found{app("sd", "fceugx-3.5", "boot.dol", "FCE Ultra GX")};
    const auto r = resolveEmulator(Console::Nes, kNoIni, found, std::nullopt);
    REQUIRE(r);
    CHECK(r->dol == "sd:/apps/fceugx-3.5/boot.dol");
}

TEST_CASE("resolveTable: N64 und Nintendont aus der Erkennung, zipConsoles aus den Profilen") {
    const std::vector<FoundApp> found{app("usb", "not64"), app("sd", "Nintendont"),
                                      app("sd", "retroarch-wii", "fceumm_libretro_wii.dol"), app("sd", "snes9xgx")};
    const EmulatorTable t = resolveTable(kNoIni, found);
    CHECK(t.forN64(N64Method::Not64)->dol == "usb:/apps/not64/boot.dol");
    CHECK(t.forN64(N64Method::Wii64)->dol == "sd:/apps/wii64/boot.dol");  // nicht gefunden: v1-Standard
    CHECK(t.nintendont().dol == "sd:/apps/nintendont/boot.dol");  // v1-Vorgabe, Datei da (FAT: Schreibweise egal)
    CHECK(t.nintendont().profile == "nintendont");
    CHECK(t.forConsole(Console::Nes)->profile == "retroarch-fceumm");
    const u8 zip = zipConsoles(t);
    CHECK((zip & (1u << static_cast<unsigned>(Console::Nes))) == 0);   // RetroArch: kein ZIP
    CHECK((zip & (1u << static_cast<unsigned>(Console::Snes))) != 0);  // Snes9x GX
    CHECK((zip & (1u << static_cast<unsigned>(Console::Gba))) != 0);   // nichts gefunden: Standard bleibt
    CHECK((zip & (1u << static_cast<unsigned>(Console::N64))) == 0);
}

TEST_CASE("zipConsoles: eigener INI-Eintrag behält das Standardbit") {
    const EmulatorTable ini = EmulatorTable::fromIni(IniDoc::parse("[nes]\ndol = sd:/apps/argv_echo/boot.dol\n"));
    const EmulatorTable t = resolveTable(ini, {});
    CHECK(zipConsoles(t) == 0b0101'0111);
}

TEST_CASE("metaXmlName: <name> per Suche, ohne XML-Bibliothek") {
    CHECK(metaXmlName("<?xml?><app><name>FCE Ultra GX</name><coder>x</coder></app>") == "FCE Ultra GX");
    CHECK(metaXmlName("<app><name>  Snes9x GX </name></app>") == "Snes9x GX");
    CHECK(metaXmlName("<app><coder>x</coder></app>").empty());
    CHECK(metaXmlName("<app><name>offen").empty());
}

TEST_CASE("scanApps: boot.dol, RetroArch-Kerne, meta.xml-Name, Gerätepräfix") {
    test::MemFileSystem fs;
    fs.addFile("usb2:/apps/fceugx/boot.dol", bytes("x"));
    fs.addFile("usb2:/apps/fceugx/meta.xml", bytes("<app><name>FCE Ultra GX</name></app>"));
    fs.addFile("usb2:/apps/retroarch-wii/fceumm_libretro_wii.dol", bytes("x"));
    fs.addFile("usb2:/apps/retroarch-wii/mgba_libretro_wii.dol", bytes("x"));
    fs.addFile("usb2:/apps/retroarch-wii/readme.txt", bytes("x"));
    fs.addFile("usb2:/apps/leer/icon.png", bytes("x"));
    fs.addFile("usb2:/apps/elf/boot.elf", bytes("x"));
    std::vector<FoundApp> apps = scanApps(fs, "usb2");
    REQUIRE(apps.size() == 4);
    auto has = [&](std::string_view dol) {
        for (const FoundApp& a : apps)
            if (a.dol == dol) return true;
        return false;
    };
    CHECK(has("usb2:/apps/fceugx/boot.dol"));
    CHECK(has("usb2:/apps/elf/boot.elf"));
    CHECK(has("usb2:/apps/retroarch-wii/fceumm_libretro_wii.dol"));
    CHECK(has("usb2:/apps/retroarch-wii/mgba_libretro_wii.dol"));
    for (const FoundApp& a : apps) {
        CHECK(a.device == "usb2");
        if (a.folder == "fceugx") CHECK(a.name == "FCE Ultra GX");
    }
    CHECK(scanApps(fs, "sd").empty());
}

TEST_CASE("scanApps: höchstens 256 Ordner, meta.xml über 64 KiB wird ignoriert") {
    test::MemFileSystem fs;
    for (int i = 0; i < 300; ++i) fs.addFile("sd:/apps/a" + std::to_string(1000 + i) + "/boot.dol", bytes("x"));
    CHECK(scanApps(fs, "sd").size() == kMaxAppFolders);
    test::MemFileSystem big;
    big.addFile("sd:/apps/x/boot.dol", bytes("x"));
    big.addFile("sd:/apps/x/meta.xml", std::vector<u8>(kMaxMetaXmlBytes + 1, ' '));
    const auto apps = scanApps(big, "sd");
    REQUIRE(apps.size() == 1);
    CHECK(apps[0].name.empty());
}
