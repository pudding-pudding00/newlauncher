#include "core/config.hpp"
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

TEST_CASE("configFromIni: [debug] log_sounds") {
    CHECK_FALSE(nl::configFromIni(nl::IniDoc::parse("")).debugLogSounds);
    CHECK(nl::configFromIni(nl::IniDoc::parse("[debug]\nlog_sounds = 1\n")).debugLogSounds);
}

TEST_CASE("configFromIni: [debug] perf") {
    CHECK_FALSE(nl::configFromIni(nl::IniDoc::parse("")).debugPerf);
    CHECK(nl::configFromIni(nl::IniDoc::parse("[debug]\nperf = 1\n")).debugPerf);
}

TEST_CASE("configFromIni: [emulators] repo, fehlt oder kein http:// → Standard") {
    CHECK(configFromIni(IniDoc::parse("")).emulatorRepo == "http://hbb1.oscwii.org/hbb");
    CHECK(configFromIni(IniDoc::parse("[emulators]\nrepo = http://example.org/hbb\n")).emulatorRepo ==
          "http://example.org/hbb");
    CHECK(configFromIni(IniDoc::parse("[emulators]\nrepo = https://x/hbb\n")).emulatorRepo ==
          "http://hbb1.oscwii.org/hbb");
    CHECK(defaultConfigText().find("[emulators]\nrepo = http://hbb1.oscwii.org/hbb\n") != std::string::npos);
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
