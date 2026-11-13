#include "ui/strings.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace nl;
using namespace nl::ui;

namespace {

// Alle Umwandlungen einer printf-Zeichenkette: '%' + Flags/Breite + Typzeichen; "%%" zählt nicht.
std::vector<std::string> conversions(const char* s) {
    std::vector<std::string> out;
    for (const char* p = s; *p != '\0'; ++p) {
        if (*p != '%') continue;
        if (p[1] == '%') {
            ++p;
            continue;
        }
        std::string conv = "%";
        ++p;
        while (*p != '\0' && std::string("-+ #0123456789.lhz").find(*p) != std::string::npos) conv += *p++;
        if (*p == '\0') break;
        conv += *p;
        out.push_back(conv);
    }
    return out;
}

struct LangGuard {
    Lang saved = language();
    ~LangGuard() { setLanguage(saved); }
};

}  // namespace

TEST_CASE("jede Str hat in DE und EN nicht-leeren Text") {
    LangGuard g;
    for (u16 i = 0; i < static_cast<u16>(Str::Count); ++i) {
        const auto s = static_cast<Str>(i);
        setLanguage(Lang::De);
        CHECK(std::string(tr(s)).size() > 0);
        setLanguage(Lang::En);
        CHECK(std::string(tr(s)).size() > 0);
    }
}

TEST_CASE("Formatfolge DE == EN") {
    LangGuard g;
    for (u16 i = 0; i < static_cast<u16>(Str::Count); ++i) {
        const auto s = static_cast<Str>(i);
        setLanguage(Lang::De);
        const auto de = conversions(tr(s));
        setLanguage(Lang::En);
        const auto en = conversions(tr(s));
        CAPTURE(i);
        CHECK(de == en);
    }
}

TEST_CASE("setLanguage schaltet tr um") {
    LangGuard g;
    setLanguage(Lang::En);
    CHECK(std::string(tr(Str::Settings)) == "Settings");
    CHECK(language() == Lang::En);
    setLanguage(Lang::De);
    CHECK(std::string(tr(Str::Settings)) == "Einstellungen");
}

TEST_CASE("coversSummaryText nennt bei Fehlern die Netzursache (Spec v2 4.4)") {
    LangGuard g;
    setLanguage(Lang::De);
    const std::string clean = coversSummaryText(3, 1, 0, NetError::None);
    CHECK(clean == "3 geladen, 1 nicht gefunden, 0 fehlgeschlagen");
    const std::string clock = coversSummaryText(0, 0, 5, NetError::Clock);
    CHECK(clock.find("5 fehlgeschlagen") != std::string::npos);
    CHECK(clock.find(tr(Str::NetErrClock)) != std::string::npos);
    // Ohne Fehlschlag keine Ursache, auch wenn eine gemeldet wurde (z. B. 404 vorher, dann Erfolg).
    CHECK(coversSummaryText(2, 0, 0, NetError::Timeout) == "2 geladen, 0 nicht gefunden, 0 fehlgeschlagen");
}

TEST_CASE("resolveLanguage") {
    CHECK(resolveLanguage(Config::Language::Auto, true) == Lang::De);
    CHECK(resolveLanguage(Config::Language::Auto, false) == Lang::En);
    CHECK(resolveLanguage(Config::Language::De, false) == Lang::De);
    CHECK(resolveLanguage(Config::Language::En, true) == Lang::En);
}

TEST_CASE("langKey") {
    CHECK(std::string(langKey(Lang::De)) == "de");
    CHECK(std::string(langKey(Lang::En)) == "en");
}

TEST_CASE("genreLabel: je kGenres-Eintrag ein Text in beiden Sprachen; außerhalb → Sonstiges") {
    setLanguage(Lang::De);
    CHECK(std::string(genreLabel(0)) == "Thema");
    CHECK(std::string(genreLabel(3)) == "Sport");
    CHECK(std::string(genreLabel(9)) == "Rollenspiel");
    CHECK(std::string(genreLabel(200)) == "Sonstiges");
    setLanguage(Lang::En);
    CHECK(std::string(genreLabel(8)) == "Simulation");
    CHECK(std::string(genreLabel(13)) == "Others");
    setLanguage(Lang::De);
}
