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
