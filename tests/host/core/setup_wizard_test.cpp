#include "core/setup_wizard.hpp"
#include <doctest/doctest.h>

#include <vector>

using namespace nl;

namespace {

SystemInfo sys() {
    SystemInfo s;
    CiosEntry d2x;
    d2x.slot = 249;
    d2x.kind = CiosKind::D2x;
    d2x.base = 56;
    CiosEntry stub;
    stub.slot = 222;
    stub.kind = CiosKind::Stub;
    s.cios = {stub, d2x};
    s.emulators = {ResolvedEmuSummary{"nes", "fceugx", "detected", "usb2:/apps/fceugx/boot.dol", "{rom}"},
                   ResolvedEmuSummary{"snes", "", "default", "sd:/apps/snes9xgx/boot.dol", ""}};
    return s;
}

DeviceGames dev(const char* name, u32 nesGames) {
    DeviceGames d;
    d.device = name;
    d.games[static_cast<std::size_t>(Console::Nes)] = nesGames;
    return d;
}

void runAll(SetupWizard& w, bool accept) {
    for (int i = 0; i < 10 && w.step() != WizardStep::Done; ++i) accept ? w.accept() : w.skip();
}

}  // namespace

TEST_CASE("SetupWizard: nur accept() übernimmt die Vorschläge") {
    const std::vector<DeviceGames> games{dev("sd", 0), dev("usb2", 12), dev("usb", 3)};
    SetupWizard w(sys(), Config{}, games);
    CHECK(w.step() == WizardStep::Language);
    runAll(w, true);
    REQUIRE(w.step() == WizardStep::Done);
    const Config c = w.result();
    CHECK(c.language == Config::Language::Auto);
    CHECK_FALSE(c.libraryRules.sources.autoMode);
    CHECK(c.libraryRules.sources.devices == std::vector<std::string>{"usb2", "usb"});
    CHECK_FALSE(c.wiiIos.has_value());
    CHECK_FALSE(w.emulatorsIni().has_value());
}

TEST_CASE("SetupWizard: nur skip() ergibt die Standard-Config") {
    Config cur;
    cur.language = Config::Language::En;
    cur.wiiIos = 249;
    cur.libraryRules.sources = SourceSpec{false, {"sd"}};
    const std::vector<DeviceGames> games{dev("usb", 5)};
    SetupWizard w(sys(), cur, games);
    runAll(w, false);
    REQUIRE(w.step() == WizardStep::Done);
    const Config c = w.result();
    const Config d;
    CHECK(c.language == d.language);
    CHECK(c.libraryRules.sources == d.libraryRules.sources);
    CHECK(c.wiiIos == d.wiiIos);
    CHECK_FALSE(w.emulatorsIni().has_value());
}

TEST_CASE("SetupWizard: back() im ersten Schritt bleibt dort, sonst einen zurück") {
    SetupWizard w(sys(), Config{}, {});
    w.back();
    CHECK(w.step() == WizardStep::Language);
    w.accept();
    CHECK(w.step() == WizardStep::Sources);
    w.back();
    CHECK(w.step() == WizardStep::Language);
}

TEST_CASE("SetupWizard: ohne Gerät mit Spielen schlägt Sources auto vor") {
    const std::vector<DeviceGames> games{dev("sd", 0), dev("usb", 0)};
    SetupWizard w(sys(), Config{}, games);
    w.accept();
    REQUIRE(w.step() == WizardStep::Sources);
    CHECK(w.selected() == w.suggested());
    runAll(w, true);
    CHECK(w.result().libraryRules.sources == SourceSpec{});
}

TEST_CASE("SetupWizard: Auswahl ändern weicht vom Vorschlag ab") {
    SetupWizard w(sys(), Config{}, {});
    w.select(1);  // Deutsch
    w.accept();
    w.accept();   // Sources: auto
    REQUIRE(w.step() == WizardStep::Cios);
    // Optionen: Automatik, IOS249 (der Stub 222 taugt nicht)
    CHECK(w.options(true).size() == 2);
    w.select(1);
    w.accept();
    REQUIRE(w.step() == WizardStep::Emulators);
    CHECK(w.options(true).size() == 3);  // erkannte verwenden, festschreiben, fehlende installieren
    w.select(2);
    CHECK(w.installSelected());
    w.select(-1);
    CHECK_FALSE(w.installSelected());
    w.accept();
    runAll(w, true);
    const Config c = w.result();
    CHECK(c.language == Config::Language::De);
    CHECK(c.wiiIos == std::optional<u8>{249});
    const auto ini = w.emulatorsIni();
    REQUIRE(ini.has_value());
    CHECK(ini->find("[nes]\ndol = usb2:/apps/fceugx/boot.dol\nargs = {rom}\n") != std::string::npos);
    CHECK(ini->find("[snes]") == std::string::npos);
}

TEST_CASE("SetupWizard: festschreiben behält eigene INI-Abschnitte") {
    SystemInfo s = sys();
    s.emulators.push_back(ResolvedEmuSummary{"n64_not64", "", "ini", "usb:/apps/not64-beta/boot.dol", "{rom}"});
    SetupWizard w(s, Config{}, {});
    for (int i = 0; i < 3; ++i) w.accept();
    REQUIRE(w.step() == WizardStep::Emulators);
    w.select(1);  // festschreiben
    runAll(w, true);
    const auto ini = w.emulatorsIni();
    REQUIRE(ini.has_value());
    CHECK(ini->find("[nes]\ndol = usb2:/apps/fceugx/boot.dol\n") != std::string::npos);
    CHECK(ini->find("[n64_not64]\ndol = usb:/apps/not64-beta/boot.dol\nargs = {rom}\n") != std::string::npos);
}

TEST_CASE("SetupWizard: emulatorsIni() ohne Abweichung ist leer") {
    SetupWizard w(sys(), Config{}, {});
    runAll(w, true);
    CHECK_FALSE(w.emulatorsIni().has_value());
}

TEST_CASE("backupName hängt .bak an") {
    CHECK(backupName("x/config.ini") == "x/config.ini.bak");
    CHECK(backupName("sd:/apps/newlauncher/emulators.ini") == "sd:/apps/newlauncher/emulators.ini.bak");
}
