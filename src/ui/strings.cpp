#include "ui/strings.hpp"

#include <cstdio>

#include "core/rating.hpp"

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

const char* genreLabel(u8 genre) {
    constexpr u16 first = static_cast<u16>(Str::GenreTheme);
    static_assert(static_cast<u16>(Str::GenreOthers) - first + 1 == kGenres.size(), "ein Text je kGenres-Eintrag");
    if (genre >= kGenres.size()) return tr(Str::GenreOthers);
    return tr(static_cast<Str>(first + genre));
}

Str netErrorText(NetError e) {
    switch (e) {
        case NetError::None: return Str::NetErrNone;
        case NetError::Dns: return Str::NetErrDns;
        case NetError::Connect: return Str::NetErrConnect;
        case NetError::Timeout: return Str::NetErrTimeout;
        case NetError::Cert: return Str::NetErrCert;
        case NetError::Clock: return Str::NetErrClock;
        case NetError::Handshake: return Str::NetErrHandshake;
        case NetError::NoMem: return Str::NetErrNoMem;
        case NetError::TlsMissing: return Str::NetErrTlsMissing;
        case NetError::Http: return Str::NetErrHttp;
        case NetError::Other: break;
    }
    return Str::NetErrOther;
}

std::string coversSummaryText(u32 ok, u32 missing, u32 failed, NetError lastError) {
    char buf[128];
    std::snprintf(buf, sizeof buf, tr(Str::CoversSummary), static_cast<unsigned>(ok), static_cast<unsigned>(missing),
                  static_cast<unsigned>(failed));
    std::string text = buf;
    if (failed > 0 && lastError != NetError::None) {
        text += '\n';
        text += tr(netErrorText(lastError));
    }
    return text;
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
