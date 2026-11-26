#pragma once
// Outbox für Uploads nach dem Spielen (M27, Spec Ziel 5): session.ini hält die laufende Sitzung, pending.ini die
// Spielschlüssel, deren Upload noch aussteht. Ein Eintrag fällt erst nach bestätigtem Upload heraus.

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.hpp"

namespace nl {

struct SessionEntry {
    std::string gameKey, unit;
    u64 started = 0;  // Unix-Zeit (nur Anzeige)
};

std::string serializeSession(const SessionEntry& e);
std::optional<SessionEntry> parseSession(std::string_view text);  // ohne game oder unit → nullopt

std::vector<std::string> parsePending(std::string_view text);
std::string serializePending(std::span<const std::string> keys);
void addPending(std::vector<std::string>& keys, std::string key);  // doppelt → bleibt einmal an alter Stelle
void removePending(std::vector<std::string>& keys, std::string_view key);

}  // namespace nl
