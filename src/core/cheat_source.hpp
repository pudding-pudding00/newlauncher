#pragma once
// Quelle der Gecko-Codelisten (Spec v2 8, M15 Task 3). Geladen wird nur auf Knopfdruck, nur über die konfigurierte
// Adresse; ohne HTTPS-Build gibt es keine Standardquelle (kein Rückfall auf HTTP).

#include <string>
#include <string_view>

#include "core/types.hpp"

namespace nl {

// Geprüft am 2026-11-13: 200 + ID6 in Zeile 1 für bekannte Spiele, 404 mit leerem Inhalt für unbekannte.
inline constexpr std::string_view kRc24CheatSource = "https://codes.rc24.xyz/txt.php?txt={id6}";
inline constexpr u32 kMaxCheatTxtBytes = 512 * 1024;

constexpr std::string_view defaultCheatSource(bool tls) { return tls ? kRc24CheatSource : std::string_view{}; }

// Ersetzt jedes „{id6}“; ohne Platzhalter bleibt die Adresse unverändert.
std::string cheatUrl(std::string_view tmpl, std::string_view id6);

enum class CheatFetchResult : u8 { Ok, NotFound, NoCodes, Exists, NetError };

// Ok nur bei lesbarer Liste mit passender ID6 und mindestens einem Code; sonst NoCodes (z. B. HTML-Fehlerseite).
CheatFetchResult classifyCheatBody(std::string_view body, std::string_view id6);
std::string_view cheatFetchTag(CheatFetchResult r);  // für NL_CHEAT_FETCH <tag>

std::string cheatTxtPath(std::string_view dataDir, std::string_view id6);  // <data>/codes/<ID6>.txt
std::string cheatSelPath(std::string_view dataDir, std::string_view id6);  // <data>/codes/<ID6>.sel

}  // namespace nl
