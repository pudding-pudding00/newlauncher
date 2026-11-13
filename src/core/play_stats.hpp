#pragma once
// Spielstatistik (M17, Spec 10.4): wie oft und wann zuletzt ein Spiel gestartet wurde, in <data>/stats.ini.
// Unabhängig vom Verlauf in history.ini (der speist „Zuletzt gespielt“ auf der Startseite).
//
// Format: je Spiel ein Abschnitt, der Schlüssel steht zwischen dem ersten „[“ und dem letzten „]“ der Zeile
// (Schlüssel dürfen „=“ und „]“ enthalten):
//   [nes/Zelda (Europe).nes]
//   starts = 12
//   last = 1762128000
// Kaputte Werte (keine Zahl, negativ, starts = 0) → Eintrag ignoriert. Zeiten in der Zukunft bleiben.

#include <map>
#include <span>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/game.hpp"

namespace nl {

struct PlayStat {
    u32 starts = 0;
    u32 last = 0;  // Unix-Zeit, 0 = unbekannt
};

class PlayStats {
public:
    static PlayStats fromText(std::string_view text);
    std::string toText() const;

    PlayStat get(std::string_view key) const;  // unbekannt → {0, 0}
    void record(std::string_view key, u32 now);
    std::size_t size() const { return stats_.size(); }

private:
    std::map<std::string, PlayStat, std::less<>> stats_;
};

enum class SortOrder : u8 { Title, Year, Recent, Most };

// Stabil; Gleichstand → Titel (compareTitles). Year: aufsteigend, unbekannt zuletzt. Recent: zuletzt
// gestartet zuerst, nie gestartete am Ende. Most: meiste Starts zuerst.
void sortGames(std::span<const Game*> v, SortOrder o, const PlayStats& s);

const char* sortOrderName(SortOrder o);  // "title", "year", "recent", "most"
std::optional<SortOrder> parseSortOrder(std::string_view s);  // getrimmt, Groß/klein egal

}  // namespace nl
