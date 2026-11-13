#include "core/cheat_source.hpp"

#include <doctest/doctest.h>

#include "core/config.hpp"
#include "core/ini.hpp"

using namespace nl;

TEST_CASE("cheatUrl: {id6} wird einmal und mehrmals ersetzt") {
    CHECK(cheatUrl("https://codes.rc24.xyz/txt.php?txt={id6}", "RMGP01") ==
          "https://codes.rc24.xyz/txt.php?txt=RMGP01");
    CHECK(cheatUrl("https://x/{id6}/{id6}.txt", "RSBP01") == "https://x/RSBP01/RSBP01.txt");
}

TEST_CASE("cheatUrl: ohne Platzhalter bleibt die Adresse wie konfiguriert") {
    CHECK(cheatUrl("https://x/codes.txt", "RMGP01") == "https://x/codes.txt");
}

TEST_CASE("defaultCheatSource: nur mit HTTPS, ohne TLS leer") {
    CHECK(defaultCheatSource(true) == "https://codes.rc24.xyz/txt.php?txt={id6}");
    CHECK(defaultCheatSource(false).empty());
}

TEST_CASE("classifyCheatBody: gültige Liste, fremde ID, HTML, leere Liste") {
    CHECK(classifyCheatBody("RMGP01\nT\n\nCode\n04001234 00000001\n", "RMGP01") == CheatFetchResult::Ok);
    CHECK(classifyCheatBody("RSBP01\nT\n\nCode\n04001234 00000001\n", "RMGP01") == CheatFetchResult::NoCodes);
    CHECK(classifyCheatBody("<html><body>Fehler</body></html>", "RMGP01") == CheatFetchResult::NoCodes);
    CHECK(classifyCheatBody("RMGP01\nT\n", "RMGP01") == CheatFetchResult::NoCodes);
    CHECK(classifyCheatBody("", "RMGP01") == CheatFetchResult::NoCodes);
}

TEST_CASE("cheatFetchTag und Pfade") {
    CHECK(cheatFetchTag(CheatFetchResult::Ok) == "ok");
    CHECK(cheatFetchTag(CheatFetchResult::NotFound) == "notfound");
    CHECK(cheatFetchTag(CheatFetchResult::NoCodes) == "nocodes");
    CHECK(cheatFetchTag(CheatFetchResult::Exists) == "exists");
    CHECK(cheatFetchTag(CheatFetchResult::NetError) == "neterror");
    CHECK(cheatTxtPath("sd:/apps/newlauncher", "RMGP01") == "sd:/apps/newlauncher/codes/RMGP01.txt");
    CHECK(cheatSelPath("sd:/apps/newlauncher", "RMGP01") == "sd:/apps/newlauncher/codes/RMGP01.sel");
}

TEST_CASE("configFromIni: [cheats] source und [debug] cheat_url/cheat_fetch") {
    CHECK(configFromIni(IniDoc::parse("")).cheatSource == defaultCheatSource(true));
    CHECK(configFromIni(IniDoc::parse(""), false).cheatSource.empty());
    CHECK(configFromIni(IniDoc::parse("[cheats]\nsource = https://c.example/{id6}.txt\n")).cheatSource ==
          "https://c.example/{id6}.txt");
    CHECK(configFromIni(IniDoc::parse("[cheats]\nsource = ftp://c/{id6}\n")).cheatSource == defaultCheatSource(true));
    const Config d = configFromIni(IniDoc::parse("[debug]\ncheat_url = https://127.0.0.1:8124/{id6}.txt\ncheat_fetch = RMGP01\n"));
    CHECK(d.debugCheatUrl == "https://127.0.0.1:8124/{id6}.txt");
    CHECK(d.debugCheatFetch == "RMGP01");
}
