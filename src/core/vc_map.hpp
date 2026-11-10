#pragma once
// VC-Injects (Spec 5.4, 6.7): Title-IDs, die Zuordnung ROM → Kanal aus n64_vc.ini und der Namensabgleich,
// mit dem der Zuordnungsdialog einen passenden Kanal vorschlägt.

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/ini.hpp"
#include "core/types.hpp"

namespace nl {

std::optional<u64> parseTitleId(std::string_view s);  // genau 16 Hex-Ziffern nach dem Trimmen, kein Präfix
std::string formatTitleId(u64 id);                     // 16 Großbuchstaben-Hex

struct ChannelInfo {
    u64 titleId = 0;
    std::string name;  // leer = Banner nicht lesbar
};

std::string channelDisplayName(const ChannelInfo& c);  // Name, sonst die Title-ID

// n64_vc.ini, Abschnitt [map]. Schlüssel: Spiel-Schlüssel n64/<datei>; ein bloßer Dateiname wird beim
// Lesen zu n64/<datei> ergänzt. Einträge mit kaputter Title-ID werden ignoriert.
class VcMap {
public:
    static VcMap fromIni(const IniDoc& doc);
    std::optional<u64> titleFor(std::string_view gameKey) const;
    void set(std::string_view gameKey, std::optional<u64> titleId);  // nullopt löscht
    IniDoc toIni() const;
    std::size_t size() const { return map_.size(); }

private:
    std::vector<std::pair<std::string, u64>> map_;  // Einfügereihenfolge
};

// Dice-Koeffizient über Zeichen-Bigramme der gefalteten Titel ohne Tags, 0..1.
float vcMatchScore(std::string_view romTitle, std::string_view channelName);
inline constexpr float kVcSuggestThreshold = 0.6f;
// Kanal mit dem höchsten Score ab kVcSuggestThreshold; bei Gleichstand der erste.
std::optional<std::size_t> suggestChannel(std::string_view romTitle, std::span<const ChannelInfo> channels);

}  // namespace nl
