#pragma once
// Unix-Zeit → Kalenderdatum (UTC), nach Howard Hinnants civil_from_days. Für ZIP-Zeiten und Sicherungsnamen (M26).

#include "core/types.hpp"

namespace nl {

struct CivilTime {
    int year;
    unsigned month, day, hour, minute, second;
};

constexpr CivilTime civilFromUnix(u64 unix) {
    const u64 days = unix / 86400;
    const u64 secs = unix % 86400;
    const long long z = static_cast<long long>(days) + 719468;
    const long long era = z / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    const int y = static_cast<int>(yoe + era * 400) + (m <= 2 ? 1 : 0);
    return CivilTime{y, m, d, static_cast<unsigned>(secs / 3600), static_cast<unsigned>(secs / 60 % 60),
                     static_cast<unsigned>(secs % 60)};
}

}  // namespace nl
