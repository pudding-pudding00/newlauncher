#include "core/sysinfo.hpp"
#include <doctest/doctest.h>

#include <regex>

using namespace nl;

namespace {

SystemInfo sample() {
    SystemInfo s;
    s.platform = Platform::Wii;
    s.sysMenuVersion = 514;
    s.region = regionFromSysMenu(514);
    s.launcherVersion = "0.13.0";
    s.launcherIos = 58;
    s.ahbprot = true;
    CiosEntry d2x;
    d2x.slot = 249;
    d2x.kind = CiosKind::D2x;
    d2x.base = 56;
    d2x.version = "d2x-v11-beta3";
    CiosEntry stub;
    stub.slot = 222;
    stub.kind = CiosKind::Stub;
    s.cios = {stub, d2x};
    s.ciosSlot = 249;
    s.gcPorts = true;
    s.devices = {DeviceSummary{"sd", "FAT", 0, 0, 12}, DeviceSummary{"usb2", "FAT", 500107862016ull, 1073741824ull, 40}};
    s.emulators = {ResolvedEmuSummary{"nes", "fceugx", "detected", "usb2:/apps/fceugx/boot.dol", ""},
                   ResolvedEmuSummary{"snes", "", "default", "sd:/apps/snes9xgx/boot.dol", ""}};
    s.netUp = true;
    s.timeSynced = true;
    s.tls = true;
    return s;
}

}  // namespace

TEST_CASE("regionFromSysMenu: 4.x- und vWii-Tabelle, sonst ?") {
    CHECK(regionFromSysMenu(512) == 'J');
    CHECK(regionFromSysMenu(513) == 'U');
    CHECK(regionFromSysMenu(514) == 'E');
    CHECK(regionFromSysMenu(518) == 'K');
    CHECK(regionFromSysMenu(608) == 'J');
    CHECK(regionFromSysMenu(609) == 'U');
    CHECK(regionFromSysMenu(610) == 'E');
    CHECK(regionFromSysMenu(481) == 'U');  // 4.2U
    CHECK(regionFromSysMenu(454) == 'K');  // 4.1K
    CHECK(regionFromSysMenu(417) == 'U');  // 4.0U
    CHECK(regionFromSysMenu(0) == '?');
    CHECK(regionFromSysMenu(515) == '?');
    CHECK(regionFromSysMenu(289) == '?');  // 3.x: nicht in der Tabelle
}

TEST_CASE("sysMenuLabel und platformForSysMenu") {
    CHECK(sysMenuLabel(513) == "4.3U");
    CHECK(sysMenuLabel(518) == "4.3K");
    CHECK(sysMenuLabel(482) == "4.2E");
    CHECK(sysMenuLabel(610) == "vWii E");
    CHECK(sysMenuLabel(608) == "vWii J");
    CHECK(sysMenuLabel(289) == "v289");
    CHECK(sysMenuLabel(0) == "?");
    CHECK(platformForSysMenu(609, false) == Platform::VWii);
    CHECK(platformForSysMenu(513, false) == Platform::Wii);
    CHECK(platformForSysMenu(513, true) == Platform::VWii);  // Latte-Chip erkannt
    CHECK(platformForSysMenu(0, false) == Platform::Unknown);
}

TEST_CASE("formatSysInfo: Plattform, System-Menü, cIOS-Tabelle, Geräte, Emulatoren (DE)") {
    const std::string t = formatSysInfo(sample(), true);
    CHECK(t.find("Plattform: Wii") != std::string::npos);
    CHECK(t.find("System-Menü: 4.3E") != std::string::npos);
    CHECK(t.find("Region: E") != std::string::npos);
    CHECK(t.find("IOS58") != std::string::npos);
    CHECK(t.find("AHBPROT: aus") != std::string::npos);
    CHECK(t.find("249 d2x base=56 d2x-v11-beta3 *") != std::string::npos);  // * = gewählter Slot
    CHECK(t.find("222 stub") != std::string::npos);
    CHECK(t.find("GameCube-Ports: ja") != std::string::npos);
    CHECK(t.find("usb2 FAT 465.8 GiB, frei 1.0 GiB, 40 Spiele") != std::string::npos);
    CHECK(t.find("sd FAT ?, 12 Spiele") != std::string::npos);
    CHECK(t.find("nes fceugx detected usb2:/apps/fceugx/boot.dol") != std::string::npos);
    CHECK(t.find("snes - default sd:/apps/snes9xgx/boot.dol") != std::string::npos);
    CHECK(t.find("newlauncher 0.13.0") != std::string::npos);
    CHECK(t.find("HTTPS: ja") != std::string::npos);
}

TEST_CASE("formatSysInfo: englisch und vWii ohne GameCube-Ports") {
    SystemInfo s = sample();
    s.platform = Platform::VWii;
    s.sysMenuVersion = 609;
    s.region = 'U';
    s.gcPorts = false;
    s.cios.clear();
    const std::string t = formatSysInfo(s, false);
    CHECK(t.find("Platform: vWii") != std::string::npos);
    CHECK(t.find("System Menu: vWii U") != std::string::npos);
    CHECK(t.find("GameCube ports: no") != std::string::npos);
    CHECK(t.find("cIOS: none") != std::string::npos);
}

TEST_CASE("formatSysInfo: nur lesend und nicht eingehängt (M16)") {
    SystemInfo s = sample();
    DeviceSummary ntfs{"usb3", "ntfs", 0, 0, 2};
    ntfs.readOnly = true;
    DeviceSummary ext{"", "ext", 0, 0, 0};
    ext.mounted = false;
    ext.reasonDe = "ext wird nicht unterstützt";
    ext.reasonEn = "ext is not supported";
    s.devices.push_back(ntfs);
    s.devices.push_back(ext);
    const std::string de = formatSysInfo(s, true);
    CHECK(de.find("usb3 ntfs ?, 2 Spiele, nur lesend") != std::string::npos);
    CHECK(de.find("- ext nicht eingehängt: ext wird nicht unterstützt") != std::string::npos);
    CHECK(de.find("usb2 FAT 465.8 GiB, frei 1.0 GiB, 40 Spiele\n") != std::string::npos);  // FAT unverändert
    const std::string en = formatSysInfo(s, false);
    CHECK(en.find("usb3 ntfs ?, 2 games, read-only") != std::string::npos);
    CHECK(en.find("- ext not mounted: ext is not supported") != std::string::npos);
}

TEST_CASE("formatSysInfo enthält keine MAC- und keine IPv4-Adresse") {
    const std::string t = formatSysInfo(sample(), true) + formatSysInfo(sample(), false);
    CHECK_FALSE(std::regex_search(t, std::regex("[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}")));
    CHECK_FALSE(std::regex_search(t, std::regex("\\b[0-9]{1,3}(\\.[0-9]{1,3}){3}\\b")));
    CHECK(t.find("Netzwerk: verbunden") != std::string::npos);
}

TEST_CASE("sysInfoLines: dieselben Zeilen wie die Datei") {
    const SystemInfo s = sample();
    std::string joined;
    for (const std::string& l : sysInfoLines(s, true)) joined += l + "\n";
    CHECK(joined == formatSysInfo(s, true));
}

TEST_CASE("emuSummaries: feste Reihenfolge, Herkunft ini/detected/default, leere Pfade fehlen") {
    EmulatorTable t;
    t.set("snes", EmulatorEntry{"sd:/apps/eigen/boot.dol", "", true, ""});
    t.set("nes", EmulatorEntry{"usb2:/apps/fceugx/boot.dol", "", false, "fceugx"});
    t.set("gba", EmulatorEntry{"sd:/apps/vbagx/boot.dol", "", false, ""});
    const auto v = emuSummaries(t);
    REQUIRE(v.size() == 3);
    CHECK(v[0].section == "nes");
    CHECK(v[0].profile == "fceugx");
    CHECK(v[0].how == "detected");
    CHECK(v[1].section == "snes");
    CHECK(v[1].how == "ini");
    CHECK(v[2].section == "gba");
    CHECK(v[2].how == "default");
    CHECK(v[2].path == "sd:/apps/vbagx/boot.dol");
}
