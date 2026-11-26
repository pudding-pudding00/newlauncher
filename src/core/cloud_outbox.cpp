#include "core/cloud_outbox.hpp"

#include <algorithm>
#include <charconv>

#include "core/ini.hpp"

namespace nl {

std::string serializeSession(const SessionEntry& e) {
    IniDoc ini;
    ini.set("session", "game", e.gameKey);
    ini.set("session", "unit", e.unit);
    ini.set("session", "started", std::to_string(e.started));
    return ini.serialize();
}

std::optional<SessionEntry> parseSession(std::string_view text) {
    const IniDoc ini = IniDoc::parse(text);
    SessionEntry e;
    e.gameKey = std::string(ini.get("session", "game"));
    e.unit = std::string(ini.get("session", "unit"));
    if (e.gameKey.empty() || e.unit.empty()) return std::nullopt;
    const std::string_view t = ini.get("session", "started");
    u64 started = 0;
    const auto [p, ec] = std::from_chars(t.data(), t.data() + t.size(), started);
    if (ec == std::errc{} && p == t.data() + t.size()) e.started = started;
    return e;
}

// Eine Zeile je Spielschlüssel; Leerzeilen und #-Kommentare zählen nicht.
std::vector<std::string> parsePending(std::string_view text) {
    std::vector<std::string> keys;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        const std::string_view line = trim(text.substr(0, nl));
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        if (!line.empty() && line.front() != '#') addPending(keys, std::string(line));
    }
    return keys;
}

std::string serializePending(std::span<const std::string> keys) {
    std::string out;
    for (const std::string& k : keys) {
        out += k;
        out += '\n';
    }
    return out;
}

void addPending(std::vector<std::string>& keys, std::string key) {
    if (key.empty() || std::find(keys.begin(), keys.end(), key) != keys.end()) return;
    keys.push_back(std::move(key));
}

void removePending(std::vector<std::string>& keys, std::string_view key) {
    keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());
}

}  // namespace nl
