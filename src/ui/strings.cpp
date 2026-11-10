#include "ui/strings.hpp"

namespace nl::ui {

namespace {

constexpr std::size_t kCount = static_cast<std::size_t>(Str::Count);

#define NL_STR_DE(id, de, en) de,
#define NL_STR_EN(id, de, en) en,
constexpr const char* kDe[kCount] = {NL_STRINGS(NL_STR_DE)};
constexpr const char* kEn[kCount] = {NL_STRINGS(NL_STR_EN)};
#undef NL_STR_DE
#undef NL_STR_EN

constexpr const char* kWeekdays[2][7] = {
    {"Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag", "Sonntag"},
    {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"},
};
constexpr const char* kMonths[2][12] = {
    {"Januar", "Februar", "März", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November",
     "Dezember"},
    {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November",
     "December"},
};

Lang gLang = Lang::De;

int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

}  // namespace

const char* tr(Str s) {
    const auto i = static_cast<std::size_t>(s);
    if (i >= kCount) return "";
    return gLang == Lang::En ? kEn[i] : kDe[i];
}

void setLanguage(Lang l) { gLang = l; }
Lang language() { return gLang; }

Lang resolveLanguage(Config::Language setting, bool systemGerman) {
    switch (setting) {
        case Config::Language::De: return Lang::De;
        case Config::Language::En: return Lang::En;
        case Config::Language::Auto: break;
    }
    return systemGerman ? Lang::De : Lang::En;
}

const char* langKey(Lang l) { return l == Lang::En ? "en" : "de"; }

const char* weekdayName(int weekday0Mon) { return kWeekdays[gLang == Lang::En][clampi(weekday0Mon, 0, 6)]; }
const char* monthName(int month1) { return kMonths[gLang == Lang::En][clampi(month1, 1, 12) - 1]; }

}  // namespace nl::ui
