#include "core/config.hpp"
#include "core/cover_download.hpp"
#include <doctest/doctest.h>
using namespace nl;

TEST_CASE("configFromIni: Standardwerte bei leerer Datei") {
    const Config c = configFromIni(IniDoc::parse(""));
    CHECK(c.language == Config::Language::Auto);
    CHECK(c.library == Config::LibraryDevice::Usb);
    CHECK(c.usbTimeoutSec == 15);
    CHECK(c.themeMode == ThemeMode::Light);
    CHECK(c.darkFromMin == 1140);
    CHECK(c.darkToMin == 420);
    CHECK(c.music);
    CHECK(c.musicVolume == 60);
    CHECK(c.sounds);
    CHECK(c.coverRegion == "auto");
}

TEST_CASE("configFromIni: defaultConfigText ergibt die Standardwerte") {
    const Config c = configFromIni(IniDoc::parse(defaultConfigText()));
    const Config d;
    CHECK(c.language == d.language);
    CHECK(c.library == d.library);
    CHECK(c.usbTimeoutSec == d.usbTimeoutSec);
    CHECK(c.themeMode == d.themeMode);
    CHECK(c.darkFromMin == d.darkFromMin);
    CHECK(c.darkToMin == d.darkToMin);
    CHECK(c.musicVolume == d.musicVolume);
    CHECK(c.coverRegion == d.coverRegion);
    CHECK(defaultConfigText().find("[general]\nlanguage = auto\nlibrary = usb\nusb_timeout = 15\n") == 0);
}

TEST_CASE("configFromIni: gesetzte Werte") {
    const Config c = configFromIni(IniDoc::parse(
        "[general]\nlanguage = de\nlibrary = sd\nusb_timeout = 30\n[theme]\nmode = auto\ndark_from = 20:30\n"
        "dark_to = 6:00\n[audio]\nmusic = 0\nmusic_volume = 80\nsounds = 0\n[covers]\nregion = DE\n"));
    CHECK(c.language == Config::Language::De);
    CHECK(c.library == Config::LibraryDevice::Sd);
    CHECK(c.usbTimeoutSec == 30);
    CHECK(c.themeMode == ThemeMode::Auto);
    CHECK(c.darkFromMin == 20 * 60 + 30);
    CHECK(c.darkToMin == 360);
    CHECK_FALSE(c.music);
    CHECK(c.musicVolume == 80);
    CHECK_FALSE(c.sounds);
    CHECK(c.coverRegion == "DE");
}

TEST_CASE("configFromIni: ungültige Werte fallen auf Standard") {
    const Config c = configFromIni(IniDoc::parse(
        "[general]\nlanguage = fr\nlibrary = nas\nusb_timeout = -3\n[theme]\nmode = blau\ndark_from = 25:00\n"
        "[audio]\nmusic_volume = 150\n[covers]\nregion = XX\n"));
    CHECK(c.language == Config::Language::Auto);
    CHECK(c.library == Config::LibraryDevice::Usb);
    CHECK(c.usbTimeoutSec == 15);
    CHECK(c.themeMode == ThemeMode::Light);
    CHECK(c.darkFromMin == 1140);
    CHECK(c.musicVolume == 60);
    CHECK(c.coverRegion == "auto");
}

TEST_CASE("configFromIni: [debug] autostart wird gelesen, fehlt → leer") {
    CHECK(nl::configFromIni(nl::IniDoc::parse("")).debugAutostart.empty());
    const auto c = nl::configFromIni(nl::IniDoc::parse("[debug]\nautostart = nes/Zelda (Europe) (Rev 1).nes\n"));
    CHECK(c.debugAutostart == "nes/Zelda (Europe) (Rev 1).nes");
}

TEST_CASE("parseClock") {
    CHECK(parseClock("07:00", -1) == 420);
    CHECK(parseClock("7:5", -1) == 425);
    CHECK(parseClock("23:59", -1) == 1439);
    CHECK(parseClock("24:00", -1) == -1);
    CHECK(parseClock("12:60", -1) == -1);
    CHECK(parseClock("ab", -1) == -1);
    CHECK(parseClock("", -1) == -1);
    CHECK(parseClock("123:00", -1) == -1);
    CHECK(parseClock(" 8:15 ", -1) == 495);
}

TEST_CASE("configFromIni: [debug] cover_url und fetch_covers") {
    const auto d = nl::configFromIni(nl::IniDoc::parse(""));
    CHECK(d.debugCoverUrl.empty());
    CHECK_FALSE(d.debugFetchCovers);
    const auto c = nl::configFromIni(nl::IniDoc::parse("[debug]\ncover_url = http://127.0.0.1:8123\nfetch_covers = 1\n"));
    CHECK(c.debugCoverUrl == "http://127.0.0.1:8123");
    CHECK(c.debugFetchCovers);
}

TEST_CASE("configFromIni: [debug] clear_covers = 0|1|2, sonst 0 (M24)") {
    CHECK(nl::configFromIni(nl::IniDoc::parse("")).debugClearCovers == 0);
    CHECK(nl::configFromIni(nl::IniDoc::parse("[debug]\nclear_covers = 1\n")).debugClearCovers == 1);
    CHECK(nl::configFromIni(nl::IniDoc::parse("[debug]\nclear_covers = 2\n")).debugClearCovers == 2);
    CHECK(nl::configFromIni(nl::IniDoc::parse("[debug]\nclear_covers = 7\n")).debugClearCovers == 0);
}

TEST_CASE("configFromIni: [debug] sd_as_disk (M16)") {
    CHECK_FALSE(nl::configFromIni(nl::IniDoc::parse("")).debugSdAsDisk);
    CHECK(nl::configFromIni(nl::IniDoc::parse("[debug]\nsd_as_disk = 1\n")).debugSdAsDisk);
    CHECK_FALSE(nl::configFromIni(nl::IniDoc::parse("[debug]\nsd_as_disk = 2\n")).debugSdAsDisk);
}

TEST_CASE("configFromIni: [debug] sysinfo_save (M13)") {
    CHECK_FALSE(nl::configFromIni(nl::IniDoc::parse("")).debugSysinfoSave);
    CHECK(nl::configFromIni(nl::IniDoc::parse("[debug]\nsysinfo_save = 1\n")).debugSysinfoSave);
    CHECK_FALSE(nl::configFromIni(nl::IniDoc::parse("[debug]\nsysinfo_save = 7\n")).debugSysinfoSave);
}

TEST_CASE("formatClock") {
    CHECK(formatClock(0) == "00:00");
    CHECK(formatClock(1140) == "19:00");
    CHECK(formatClock(1410) == "23:30");
}

TEST_CASE("configToIniText erhält [debug], Kommentare und unbekannte Schlüssel") {
    const std::string before =
        "; Kommentar oben\n[general]\nlanguage = auto\nfoo = 1\n# noch ein Kommentar\n[debug]\nautostart = nes/x.nes\n";
    const std::string after = configToIniText(Config{}, before);
    CHECK(after.find("; Kommentar oben\n") != std::string::npos);
    CHECK(after.find("# noch ein Kommentar\n") != std::string::npos);
    CHECK(after.find("foo = 1\n") != std::string::npos);
    CHECK(after.find("[debug]\nautostart = nes/x.nes\n") != std::string::npos);
    // Fehlende Abschnitte und Schlüssel kommen dazu.
    CHECK(after.find("[audio]") != std::string::npos);
    CHECK(after.find("region = auto") != std::string::npos);
}

TEST_CASE("configToIniText Round-Trip") {
    Config c;
    c.language = Config::Language::En;
    c.themeMode = ThemeMode::Auto;
    c.darkFromMin = 19 * 60 + 30;
    c.music = false;
    c.musicVolume = 40;
    c.sounds = false;
    c.library = Config::LibraryDevice::Sd;
    c.coverRegion = "FR";
    const std::string text = configToIniText(c, defaultConfigText());
    const Config r = configFromIni(IniDoc::parse(text));
    CHECK(r.language == Config::Language::En);
    CHECK(r.themeMode == ThemeMode::Auto);
    CHECK(r.darkFromMin == 19 * 60 + 30);
    CHECK(r.darkToMin == 7 * 60);
    CHECK(r.music == false);
    CHECK(r.musicVolume == 40);
    CHECK(r.sounds == false);
    CHECK(r.library == Config::LibraryDevice::Sd);
    CHECK(r.coverRegion == "FR");
    CHECK(text.find("language = en") != std::string::npos);
    CHECK(text.find("dark_from = 19:30") != std::string::npos);
    CHECK(text.find("music = 0") != std::string::npos);
    CHECK(text.find("region = FR") != std::string::npos);
}

TEST_CASE("configToIniText: ungültiger Wert wird als Standard geschrieben") {
    const std::string before = "[audio]\nmusic_volume = 300\n";
    const Config c = configFromIni(IniDoc::parse(before));
    const std::string after = configToIniText(c, before);
    CHECK(after.find("music_volume = 60") != std::string::npos);
    CHECK(after.find("300") == std::string::npos);
}

TEST_CASE("configToIniText behält CRLF und schreibt jeden Schlüssel nur einmal") {
    const std::string before = "[general]\r\nlanguage = de\r\n[theme]\r\nmode = dark\r\n";
    const std::string after = configToIniText(Config{}, before);
    CHECK(after.find("language = auto\r\n") != std::string::npos);
    CHECK(after.find("mode = light\r\n") != std::string::npos);
    CHECK(after.find("\n[") == after.find("\r\n[") + 1);
    std::size_t n = 0;
    for (std::size_t p = after.find("language ="); p != std::string::npos; p = after.find("language =", p + 1)) ++n;
    CHECK(n == 1);
}

TEST_CASE("configFromIni: [debug] list_games") {
    CHECK_FALSE(nl::configFromIni(nl::IniDoc::parse("")).debugListGames);
    CHECK(nl::configFromIni(nl::IniDoc::parse("[debug]\nlist_games = 1\n")).debugListGames);
}

TEST_CASE("configFromIni: [debug] log_sounds") {
    CHECK_FALSE(nl::configFromIni(nl::IniDoc::parse("")).debugLogSounds);
    CHECK(nl::configFromIni(nl::IniDoc::parse("[debug]\nlog_sounds = 1\n")).debugLogSounds);
}

TEST_CASE("configFromIni: [debug] perf") {
    CHECK_FALSE(nl::configFromIni(nl::IniDoc::parse("")).debugPerf);
    CHECK(nl::configFromIni(nl::IniDoc::parse("[debug]\nperf = 1\n")).debugPerf);
}

TEST_CASE("configFromIni: [debug] remote") {
    CHECK_FALSE(nl::configFromIni(nl::IniDoc::parse("")).debugRemote);
    CHECK(nl::configFromIni(nl::IniDoc::parse("[debug]\nremote = 1\n")).debugRemote);
    CHECK(nl::configFromIni(nl::IniDoc::parse("")).debugUpdateUrl.empty());
    CHECK_FALSE(nl::configFromIni(nl::IniDoc::parse("")).debugSelfUpdate);
    CHECK(nl::configFromIni(nl::IniDoc::parse("[debug]\nself_update = 1\n")).debugSelfUpdate);
    CHECK(nl::configFromIni(nl::IniDoc::parse("[debug]\nupdate_url = https://127.0.0.1:8443/update.ini \n"))
              .debugUpdateUrl == "https://127.0.0.1:8443/update.ini");
}

TEST_CASE("configFromIni: [emulators] repo, fehlt oder weder http:// noch https:// → Standard") {
    CHECK(configFromIni(IniDoc::parse("")).emulatorRepo == "https://hbb1.oscwii.org/hbb");
    CHECK(configFromIni(IniDoc::parse("[emulators]\nrepo = http://example.org/hbb\n")).emulatorRepo ==
          "http://example.org/hbb");
    CHECK(configFromIni(IniDoc::parse("[emulators]\nrepo = https://x/hbb\n")).emulatorRepo == "https://x/hbb");
    CHECK(configFromIni(IniDoc::parse("[emulators]\nrepo = ftp://x/hbb\n")).emulatorRepo ==
          "https://hbb1.oscwii.org/hbb");
    CHECK(configFromIni(IniDoc::parse("[emulators]\nrepo = https://\n")).emulatorRepo ==
          "https://hbb1.oscwii.org/hbb");
    CHECK(defaultConfigText().find("[emulators]\nrepo = https://hbb1.oscwii.org/hbb\n") != std::string::npos);
}

TEST_CASE("configFromIni: [debug] emu_repo und emu_install") {
    const Config c = configFromIni(IniDoc::parse("[debug]\nemu_repo = http://127.0.0.1:8080/hbb\nemu_install = fceugx\n"));
    CHECK(c.debugEmuRepo == "http://127.0.0.1:8080/hbb");
    CHECK(c.debugEmuInstall == "fceugx");
}

TEST_CASE("configToIniText: schreibt [emulators] repo") {
    Config c;
    c.emulatorRepo = "http://example.org/hbb";
    const std::string text = configToIniText(c, "[general]\nlanguage = auto\n");
    CHECK(configFromIni(IniDoc::parse(text)).emulatorRepo == "http://example.org/hbb");
}

TEST_CASE("configFromIni: [debug] ignore_ahbprot") {
    CHECK_FALSE(configFromIni(IniDoc::parse("")).debugIgnoreAhbprot);
    CHECK(configFromIni(IniDoc::parse("[debug]\nignore_ahbprot = 1\n")).debugIgnoreAhbprot);
    CHECK_FALSE(configFromIni(IniDoc::parse("[debug]\nignore_ahbprot = 2\n")).debugIgnoreAhbprot);
}

TEST_CASE("configFromIni: [debug] wii_dry_run") {
    CHECK_FALSE(configFromIni(IniDoc::parse("")).debugWiiDryRun);
    CHECK(configFromIni(IniDoc::parse("[debug]\nwii_dry_run = 1\n")).debugWiiDryRun);
    CHECK_FALSE(configFromIni(IniDoc::parse("[debug]\nwii_dry_run = 2\n")).debugWiiDryRun);
}

TEST_CASE("configFromIni: [debug] tls_ca und [covers] base (M11)") {
    const Config d = configFromIni(IniDoc::parse(""));
    CHECK(d.debugTlsCa.empty());
    CHECK(d.coverBase == defaultCoverBase(true));
    const Config c = configFromIni(IniDoc::parse(
        "[debug]\ntls_ca = sd:/apps/newlauncher/test-ca.pem\n[covers]\nbase = https://covers.example/wii/cover\n"));
    CHECK(c.debugTlsCa == "sd:/apps/newlauncher/test-ca.pem");
    CHECK(c.coverBase == "https://covers.example/wii/cover");
    CHECK(configFromIni(IniDoc::parse("[covers]\nbase = ftp://x/\n")).coverBase == defaultCoverBase(true));
}

TEST_CASE("configFromIni: [debug] open_scene (M11)") {
    CHECK(configFromIni(IniDoc::parse("")).debugOpenScene.empty());
    CHECK(configFromIni(IniDoc::parse("[debug]\nopen_scene = about\n")).debugOpenScene == "about");
}

TEST_CASE("configFromIni: [debug] banner_file (M18)") {
    CHECK(configFromIni(IniDoc::parse("")).debugBannerFile.empty());
    CHECK(configFromIni(IniDoc::parse("[debug]\nbanner_file =  sd:/x/banner.app \n")).debugBannerFile ==
          "sd:/x/banner.app");
}

TEST_CASE("configFromIni: [debug] filter (M17)") {
    CHECK(configFromIni(IniDoc::parse("")).debugFilter.empty());
    CHECK(configFromIni(IniDoc::parse("[debug]\nfilter = category:Party \n")).debugFilter == "category:Party");
}

TEST_CASE("Standardquellen: HTTPS mit TLS, HTTP ohne (M11 Task 8)") {
    const Config on = configFromIni(IniDoc::parse(""), true);
    CHECK(on.emulatorRepo == "https://hbb1.oscwii.org/hbb");
    CHECK(on.coverBase == "https://art.gametdb.com/wii/cover");
    const Config off = configFromIni(IniDoc::parse(""), false);
    CHECK(off.emulatorRepo == "http://hbb1.oscwii.org/hbb");
    CHECK(off.coverBase == "http://art.gametdb.com/wii/cover");
    CHECK(defaultConfigText(true).find("repo = https://hbb1.oscwii.org/hbb\n") != std::string::npos);
    CHECK(defaultConfigText(false).find("repo = http://hbb1.oscwii.org/hbb\n") != std::string::npos);
    CHECK(defaultEmuRepo(true) == "https://hbb1.oscwii.org/hbb");
    CHECK(defaultCoverBase(false) == "http://art.gametdb.com/wii/cover");
}

TEST_CASE("Standardquellen: alte Standard-URL in config.ini folgt dem Build (M11 Task 8)") {
    // M10 schrieb "repo = http://hbb1.oscwii.org/hbb" aus; ein TLS-Build nimmt dafür HTTPS, ein Build ohne TLS HTTP.
    CHECK(configFromIni(IniDoc::parse("[emulators]\nrepo = http://hbb1.oscwii.org/hbb\n"), true).emulatorRepo ==
          "https://hbb1.oscwii.org/hbb");
    CHECK(configFromIni(IniDoc::parse("[emulators]\nrepo = https://hbb1.oscwii.org/hbb\n"), false).emulatorRepo ==
          "http://hbb1.oscwii.org/hbb");
    CHECK(configFromIni(IniDoc::parse("[covers]\nbase = http://art.gametdb.com/wii/cover\n"), true).coverBase ==
          "https://art.gametdb.com/wii/cover");
    // Fremde Quellen bleiben, wie sie sind – auch http:// mit TLS-Build (kein stilles Umschreiben).
    CHECK(configFromIni(IniDoc::parse("[emulators]\nrepo = http://example.org/hbb\n"), true).emulatorRepo ==
          "http://example.org/hbb");
    CHECK(configFromIni(IniDoc::parse("[emulators]\nrepo = https://example.org/hbb\n"), false).emulatorRepo ==
          "https://example.org/hbb");
}


TEST_CASE("Config: [library] sources, [paths] und Alt-Schlüssel") {
    const Config d = configFromIni(IniDoc::parse(defaultConfigText()));
    CHECK(d.libraryRules.sources.autoMode);
    const Config v1 = configFromIni(IniDoc::parse("[general]\nlibrary = sd\n"));
    CHECK(v1.library == Config::LibraryDevice::Sd);
    CHECK(v1.libraryRules.sources.devices == std::vector<std::string>{"sd"});
    const Config p = configFromIni(IniDoc::parse("[paths]\nwii = games/wii|wbfs\n"));
    CHECK(p.libraryRules.paths[static_cast<std::size_t>(Console::Wii)].folders.size() == 2);
}

TEST_CASE("configToIniText schreibt [library] und bewahrt Kommentare und [paths]") {
    const std::string before = "; oben\n[general]\nlanguage = auto\n[paths]\n; eigene Ordner\nwii = games/wii\n";
    Config c = configFromIni(IniDoc::parse(before));
    c.libraryRules.sources = SourceSpec{false, {"usb2", "sd"}};
    const std::string after = configToIniText(c, before);
    CHECK(after.find("; oben\n") != std::string::npos);
    CHECK(after.find("; eigene Ordner\nwii = games/wii\n") != std::string::npos);
    CHECK(after.find("[library]\nsources = usb2,sd\n") != std::string::npos);
    const Config r = configFromIni(IniDoc::parse(after));
    CHECK(r.libraryRules.sources.devices == std::vector<std::string>{"usb2", "sd"});
}

TEST_CASE("Config: [wii] ios (M13, Spec 6.2)") {
    CHECK_FALSE(configFromIni(IniDoc::parse("")).wiiIos.has_value());
    CHECK_FALSE(configFromIni(IniDoc::parse("[wii]\nios = auto\n")).wiiIos.has_value());
    const Config c = configFromIni(IniDoc::parse("[wii]\nios = 250\n"));
    CHECK(c.wiiIos == std::optional<u8>(250));
    CHECK(c.badKeys.empty());
    const Config bad = configFromIni(IniDoc::parse("[wii]\nios = 199\n"));
    CHECK_FALSE(bad.wiiIos.has_value());
    CHECK(bad.badKeys == std::vector<std::string>{"wii.ios"});
    CHECK(configToIniText(c, "").find("[wii]\nios = 250\n") != std::string::npos);
    CHECK(configToIniText(Config{}, "").find("[wii]\nios = auto\n") != std::string::npos);
}

TEST_CASE("applyDebugIni: nur [debug] aus debug.ini, wizard-Schlüssel (M13 Task 5)") {
    Config c;
    c.language = Config::Language::De;
    applyDebugIni(c, IniDoc::parse("[debug]\nwizard = accept\nsysinfo_save = 1\n[general]\nlanguage = en\n"));
    CHECK(c.debugWizard == "accept");
    CHECK(c.debugSysinfoSave);
    CHECK(c.language == Config::Language::De);
    CHECK(configFromIni(IniDoc::parse("[debug]\nwizard = accept\n")).debugWizard == "accept");
}

TEST_CASE("configFromIni: [debug] option_set (M14)") {
    CHECK(configFromIni(IniDoc::parse("")).debugOptionSet.empty());
    CHECK(configFromIni(IniDoc::parse("[debug]\noption_set = video=pal60\n")).debugOptionSet == "video=pal60");
}

TEST_CASE("Config: [library] sort (M17)") {
    CHECK(configFromIni(IniDoc::parse("")).sortOrder == SortOrder::Title);
    CHECK(configFromIni(IniDoc::parse("[library]\nsort = most\n")).sortOrder == SortOrder::Most);
    const Config bad = configFromIni(IniDoc::parse("[library]\nsort = zufall\n"));
    CHECK(bad.sortOrder == SortOrder::Title);
    Config c;
    c.sortOrder = SortOrder::Recent;
    CHECK(configToIniText(c, "").find("sort = recent") != std::string::npos);
}

TEST_CASE("Config: [parental] lesen und schreiben, [debug] pin_input (M17)") {
    CHECK_FALSE(configFromIni(IniDoc::parse("")).parental.enabled);
    Config c;
    constexpr std::array<u8, 16> salt = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    c.parental = withPin(c.parental, "1234", salt);
    c.parental.maxAge = 12;
    c.parental.system = RatingSystem::Usk;
    const std::string text = configToIniText(c, "");
    CHECK(text.find("[parental]") != std::string::npos);
    CHECK(text.find("1234") == std::string::npos);  // nie die PIN selbst
    const Config r = configFromIni(IniDoc::parse(text));
    CHECK(checkPin(r.parental, "1234"));
    CHECK(r.parental.maxAge == 12);
    CHECK(r.parental.system == RatingSystem::Usk);
    CHECK(configFromIni(IniDoc::parse("[debug]\npin_input = 0000\n")).debugPinInput == "0000");
}

TEST_CASE("Config: [emunand] path lesen, schreiben und leer weglassen (M19)") {
    CHECK(configFromIni(IniDoc::parse("")).emuNandPath.empty());
    const Config loaded = configFromIni(IniDoc::parse("[emunand]\npath = usb:/nands/emu1\n"));
    CHECK(loaded.emuNandPath == "usb:/nands/emu1");
    const std::string written = configToIniText(loaded, "");
    CHECK(written.find("[emunand]\npath = usb:/nands/emu1\n") != std::string::npos);
    CHECK(configFromIni(IniDoc::parse(written)).emuNandPath == "usb:/nands/emu1");

    const std::string cleared = configToIniText(Config{}, "[emunand]\npath = usb:/nands/emu1\n");
    CHECK(configFromIni(IniDoc::parse(cleared)).emuNandPath.empty());
}

TEST_CASE("Config: [debug] pin_input wird beim Schreiben entfernt, andere debug-Zeilen bleiben") {
    const Config c = configFromIni(IniDoc::parse("[debug]\npin_input = 1234\nperf = 1\n"));
    const std::string text = configToIniText(c, "[debug]\npin_input = 1234\nperf = 1\n");
    CHECK(text.find("pin_input") == std::string::npos);
    CHECK(text.find("1234") == std::string::npos);
    CHECK(text.find("perf = 1") != std::string::npos);
}

TEST_CASE("Config: [parental] lock_until übersteht das Schreiben, wird bei 0 nicht geschrieben und entfernt (M17-Review)") {
    Config c;
    CHECK(configFromIni(IniDoc::parse("")).parental.lockUntil == 0);
    c.parental.lockUntil = 1762700030;
    const std::string text = configToIniText(c, "");
    CHECK(text.find("lock_until = 1762700030") != std::string::npos);
    CHECK(configFromIni(IniDoc::parse(text)).parental.lockUntil == 1762700030u);
    c.parental.lockUntil = 0;
    CHECK(configToIniText(c, "").find("lock_until") == std::string::npos);
    const std::string cleared = configToIniText(c, text);
    CHECK(cleared.find("lock_until") == std::string::npos);
    CHECK(configFromIni(IniDoc::parse(cleared)).parental.lockUntil == 0);
    CHECK(configFromIni(IniDoc::parse("[parental]\nlock_until = x\n")).parental.lockUntil == 0);
}

TEST_CASE("configFromIni: [ui] banners (M18), Standard 1, Round-Trip") {
    CHECK(configFromIni(IniDoc::parse("")).banners);
    CHECK(configFromIni(IniDoc::parse(defaultConfigText())).banners);
    CHECK_FALSE(configFromIni(IniDoc::parse("[ui]\nbanners = 0\n")).banners);
    CHECK(configFromIni(IniDoc::parse("[ui]\nbanners = 7\n")).banners);  // ungültig → Standard
    CHECK(defaultConfigText().find("[ui]\nbanners = 1\n") != std::string::npos);
    Config c;
    c.banners = false;
    const std::string text = configToIniText(c, defaultConfigText());
    CHECK(text.find("banners = 0") != std::string::npos);
    CHECK_FALSE(configFromIni(IniDoc::parse(text)).banners);
}

TEST_CASE("configFromIni: [covers] auto (M21), Standard 1, Round-Trip") {
    CHECK(configFromIni(IniDoc::parse("")).coverAuto);
    CHECK(configFromIni(IniDoc::parse(defaultConfigText())).coverAuto);
    CHECK_FALSE(configFromIni(IniDoc::parse("[covers]\nauto = 0\n")).coverAuto);
    CHECK(configFromIni(IniDoc::parse("[covers]\nauto = 7\n")).coverAuto);  // ungültig → Standard
    CHECK(defaultConfigText().find("[covers]\nregion = auto\nauto = 1\n") != std::string::npos);
    Config c;
    c.coverAuto = false;
    const std::string text = configToIniText(c, defaultConfigText());
    CHECK_FALSE(configFromIni(IniDoc::parse(text)).coverAuto);
    CHECK_FALSE(configFromIni(IniDoc::parse(configToIniText(c, ""))).coverAuto);
}

TEST_CASE("configFromIni: [debug] details_back_ms (M18-Smoke)") {
    CHECK(configFromIni(IniDoc::parse("")).debugDetailsBackMs == 0);
    CHECK(configFromIni(IniDoc::parse("[debug]\ndetails_back_ms = 4000\n")).debugDetailsBackMs == 4000);
    CHECK(configFromIni(IniDoc::parse("[debug]\ndetails_back_ms = 999999\n")).debugDetailsBackMs == 0);
}

TEST_CASE("configFromIni: [covers] retro_base, nur HTTPS-Standard mit TLS (M22)") {
    CHECK(configFromIni(IniDoc::parse(""), true).retroCoverBase ==
          "https://raw.githubusercontent.com/libretro-thumbnails");
    CHECK(configFromIni(IniDoc::parse(""), false).retroCoverBase.empty());  // kein Rückfall auf HTTP
    CHECK(configFromIni(IniDoc::parse("[covers]\nretro_base = https://mirror.example/thumbs\n"), false)
              .retroCoverBase == "https://mirror.example/thumbs");
    CHECK(configFromIni(IniDoc::parse("[covers]\nretro_base = quatsch\n"), true).retroCoverBase ==
          "https://raw.githubusercontent.com/libretro-thumbnails");
    // Wird nicht in die config.ini geschrieben, wie [covers] base
    CHECK(configToIniText(configFromIni(IniDoc::parse("")), "").find("retro_base") == std::string::npos);
}

TEST_CASE("configFromIni: [covers] storage (M24), Standard sd, Round-Trip") {
    CHECK(configFromIni(IniDoc::parse("")).coverStorage == CoverStorage::Sd);
    CHECK(configFromIni(IniDoc::parse(defaultConfigText())).coverStorage == CoverStorage::Sd);
    CHECK(configFromIni(IniDoc::parse("[covers]\nstorage = device\n")).coverStorage == CoverStorage::Device);
    CHECK(configFromIni(IniDoc::parse("[covers]\nstorage = usb\n")).coverStorage == CoverStorage::Sd);  // ungültig
    CHECK(defaultConfigText().find("auto = 1\nstorage = sd\n") != std::string::npos);
    Config c;
    c.coverStorage = CoverStorage::Device;
    CHECK(configFromIni(IniDoc::parse(configToIniText(c, defaultConfigText()))).coverStorage == CoverStorage::Device);
    CHECK(configFromIni(IniDoc::parse(configToIniText(c, ""))).coverStorage == CoverStorage::Device);
}

TEST_CASE("M26: device_name und Debug-Schlüssel save_backup/save_restore") {
    const Config c = configFromIni(IniDoc::parse(
        "[general]\ndevice_name =  Wohnzimmer-Wii \n[debug]\nsave_backup = RMGP01\nsave_restore = latest\n"));
    CHECK(c.deviceName == "Wohnzimmer-Wii");
    CHECK(c.debugSaveBackup == "RMGP01");
    CHECK(c.debugSaveRestore == "latest");
    CHECK(configFromIni(IniDoc::parse("")).deviceName.empty());
    // zu lang → auf 32 Zeichen gekürzt; Steuerzeichen und ']' / '=' fliegen raus (landet in newlauncher-save.ini)
    const Config l = configFromIni(IniDoc::parse("[general]\ndevice_name = " + std::string(40, 'x') + "\n"));
    CHECK(l.deviceName.size() == 32);
    CHECK(configFromIni(IniDoc::parse("[general]\ndevice_name = a=b]c\n")).deviceName == "abc");
    // 31 × 'x' + "ü" (2 Bytes) → das halbe ü fällt weg
    CHECK(configFromIni(IniDoc::parse("[general]\ndevice_name = " + std::string(31, 'x') + "\xC3\xBC\n")).deviceName ==
          std::string(31, 'x'));
}
