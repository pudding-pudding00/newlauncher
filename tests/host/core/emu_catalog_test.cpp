#include "core/emu_catalog.hpp"
#include <doctest/doctest.h>
#include <set>
using namespace nl;

namespace {
// Auszug aus der echten listv036.txt (CRLF), gekürzte Beschreibungen.
const std::string kList =
    "Homebrew 2092896 v0.3.9e | - Updated with latest libogc\r\n"
    "=Emulators=\r\n"
    "fceugx 1793664000 8367 5065728 dol 2849329 56463 0 wwwwngcs /fceugx;/fceugx/roms;/apps/fceugx; . .\r\n"
    "FCE Ultra GX\r\n"
    "Tantric, Zopenko, Askot, others\r\n"
    "4.0.4\r\n"
    "5074467\r\n"
    "Nintendo Emulator\r\n"
    "A port of FCE Ultra to the Wii.\r\n"
    "=Utilities=\r\n"
    "Nintendont 1482019200 1156 1648128 dol 1077171 213279 0 wwwwngcsk /controllers;/apps/Nintendont; . .\r\n"
    "Nintendont\r\n"
    "FIX94, crediar\r\n"
    "Newer than 4.431\r\n"
    "1649864\r\n"
    "Gamecube Loader\r\n"
    "Nintendont allows you to run GameCube games.\r\n";

const OscApp* find(const std::vector<OscApp>& apps, std::string_view name) {
    for (const OscApp& a : apps)
        if (a.name == name) return &a;
    return nullptr;
}

const EmuRow& row(const std::vector<EmuRow>& rows, std::string_view name) {
    for (const EmuRow& r : rows)
        if (r.info->oscName == name) return r;
    FAIL("Zeile fehlt");
    return rows.front();
}
}  // namespace

TEST_CASE("parseOscList: echte Blöcke mit CRLF") {
    const auto apps = parseOscList(kList);
    REQUIRE(apps.size() == 2);
    const OscApp* f = find(apps, "fceugx");
    REQUIRE(f != nullptr);
    CHECK(f->timestamp == 1793664000u);
    CHECK(f->unzippedSize == 5065728u);
    CHECK(f->zipSize == 2849329u);
    CHECK(f->displayName == "FCE Ultra GX");
    CHECK(f->version == "4.0.4");
    const OscApp* n = find(apps, "Nintendont");
    REQUIRE(n != nullptr);
    CHECK(n->version == "Newer than 4.431");
    CHECK(n->zipSize == 1077171u);
}

TEST_CASE("parseOscList: LF statt CRLF") {
    std::string lf;
    for (char c : kList)
        if (c != '\r') lf += c;
    CHECK(parseOscList(lf).size() == 2);
}

TEST_CASE("parseOscList: kaputter Block wird übersprungen, der nächste gelesen") {
    std::string text = kList;
    const std::size_t at = text.find("1793664000");
    text.replace(at, 10, "17936x4000");
    const auto apps = parseOscList(text);
    REQUIRE(apps.size() == 1);
    CHECK(apps[0].name == "Nintendont");
}

TEST_CASE("parseOscList: abgeschnittener letzter Block wird verworfen") {
    const std::string cut = kList.substr(0, kList.find("Gamecube Loader"));
    const auto apps = parseOscList(cut);
    REQUIRE(apps.size() == 1);
    CHECK(apps[0].name == "fceugx");
    CHECK(parseOscList("").empty());
    CHECK(parseOscList("Homebrew 1 v\r\n").empty());
}

TEST_CASE("emulatorCatalog: Reihenfolge und DOL-Pfade wie Spec 6.9") {
    const auto cat = emulatorCatalog();
    REQUIRE(cat.size() == 6);
    CHECK(cat[0].oscName == "fceugx");
    CHECK(cat[0].dol == "sd:/apps/fceugx/boot.dol");
    CHECK(cat[1].oscName == "snes9xgx");
    CHECK(cat[2].oscName == "vbagx");
    CHECK(cat[3].oscName == "not64");
    CHECK(cat[4].oscName == "wii64");
    CHECK(cat[5].oscName == "Nintendont");
    CHECK(cat[5].dol == "sd:/apps/Nintendont/boot.dol");
}

TEST_CASE("installierte Emulatoren: Round-Trip") {
    std::map<std::string, InstalledEmu> m;
    m["fceugx"] = InstalledEmu{1793664000u, "4.0.4"};
    m["Nintendont"] = InstalledEmu{1482019200u, ""};
    const auto back = parseInstalledEmus(formatInstalledEmus(m));
    REQUIRE(back.size() == 2);
    CHECK(back.at("fceugx").timestamp == 1793664000u);
    CHECK(back.at("fceugx").version == "4.0.4");
    CHECK(back.at("Nintendont").version.empty());
    CHECK(parseInstalledEmus("[x]\ntimestamp = abc\n").count("x") == 0);
}

TEST_CASE("emuRows: Status aus lokalem Stand und Liste") {
    const auto list = parseOscList(kList);
    std::map<std::string, InstalledEmu> inst;
    inst["fceugx"] = InstalledEmu{1700000000u, "4.0.0"};
    inst["Nintendont"] = InstalledEmu{1482019200u, "x"};
    const std::set<std::string> dols = {"sd:/apps/fceugx/boot.dol", "sd:/apps/Nintendont/boot.dol",
                                        "sd:/apps/snes9xgx/boot.dol"};
    auto exists = [&](std::string_view d) { return dols.count(std::string(d)) > 0; };
    const auto rows = emuRows(inst, exists, list);
    REQUIRE(rows.size() == 6);
    CHECK(row(rows, "fceugx").state == EmuState::UpdateAvailable);
    REQUIRE(row(rows, "fceugx").remote.has_value());
    CHECK(row(rows, "Nintendont").state == EmuState::Current);
    CHECK(row(rows, "snes9xgx").state == EmuState::UnknownVersion);
    CHECK(!row(rows, "snes9xgx").remote.has_value());  // nicht in der Liste
    CHECK(row(rows, "vbagx").state == EmuState::NotInstalled);
}

TEST_CASE("emuRows: ohne Liste nur der lokale Stand") {
    std::map<std::string, InstalledEmu> inst;
    inst["fceugx"] = InstalledEmu{1u, "a"};
    inst["vbagx"] = InstalledEmu{1u, "b"};  // Eintrag, aber DOL gelöscht
    auto exists = [](std::string_view d) { return d == "sd:/apps/fceugx/boot.dol"; };
    const auto rows = emuRows(inst, exists, {});
    CHECK(row(rows, "fceugx").state == EmuState::Current);
    CHECK(row(rows, "vbagx").state == EmuState::NotInstalled);
}

TEST_CASE("OSC-URLs") {
    CHECK(oscListUrl("http://h/hbb") == "http://h/hbb/listv036.txt");
    CHECK(oscListUrl("http://h/hbb/") == "http://h/hbb/listv036.txt");
    CHECK(oscZipUrl("http://h/hbb/", "Nintendont") == "http://h/hbb/Nintendont/Nintendont.zip");
}
