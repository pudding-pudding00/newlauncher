#include "core/vc_map.hpp"

#include <algorithm>

#include "core/text_fold.hpp"

namespace nl {

namespace {

constexpr const char* kSection = "map";
constexpr std::string_view kN64Prefix = "n64/";

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Entfernt alle geschlossenen (…)/[…]-Gruppen. Anders als cleanTitle ohne Rückfall auf den Originaltext:
// ein Titel nur aus Tags soll beim Abgleich leer sein, nicht „europe“ heißen.
std::string stripTags(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '(' || c == '[') {
            const std::size_t close = s.find(c == '(' ? ')' : ']', i + 1);
            if (close != std::string_view::npos) {
                i = close;
                out += ' ';
                continue;
            }
        }
        out += c;
    }
    return out;
}

// Bigramme aus zwei Zeichen ohne Leerzeichen, sortiert (Multimenge).
std::vector<u16> bigrams(std::string_view title) {
    const std::string f = foldForSearch(stripTags(title));
    std::vector<u16> out;
    for (std::size_t i = 0; i + 1 < f.size(); ++i) {
        if (f[i] == ' ' || f[i + 1] == ' ') continue;
        out.push_back(static_cast<u16>((static_cast<u8>(f[i]) << 8) | static_cast<u8>(f[i + 1])));
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

std::optional<u64> parseTitleId(std::string_view s) {
    s = trim(s);
    if (s.size() != 16) return std::nullopt;
    u64 v = 0;
    for (char c : s) {
        const int h = hexValue(c);
        if (h < 0) return std::nullopt;
        v = (v << 4) | static_cast<u64>(h);
    }
    return v;
}

std::string formatTitleId(u64 id) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = kHex[id & 0xF];
        id >>= 4;
    }
    return out;
}

std::string channelDisplayName(const ChannelInfo& c) { return c.name.empty() ? formatTitleId(c.titleId) : c.name; }

VcMap VcMap::fromIni(const IniDoc& doc) {
    VcMap m;
    for (const auto& [k, v] : doc.entries(kSection)) {
        const auto id = parseTitleId(v);
        if (!id || k.empty()) continue;
        if (k.find('/') == std::string::npos) m.set(std::string(kN64Prefix) + k, *id);
        else m.set(k, *id);
    }
    return m;
}

std::optional<u64> VcMap::titleFor(std::string_view gameKey) const {
    for (const auto& [k, id] : map_)
        if (k == gameKey) return id;
    return std::nullopt;
}

void VcMap::set(std::string_view gameKey, std::optional<u64> titleId) {
    for (auto it = map_.begin(); it != map_.end(); ++it) {
        if (it->first != gameKey) continue;
        if (titleId) it->second = *titleId;
        else map_.erase(it);
        return;
    }
    if (titleId) map_.emplace_back(std::string(gameKey), *titleId);
}

IniDoc VcMap::toIni() const { return IniDoc::parse(serialize({})); }

std::string VcMap::serialize(std::string_view original) const {
    std::string out;
    std::vector<bool> written(map_.size(), false);
    auto entryLine = [&](std::size_t i) {
        out += map_[i].first + " = " + formatTitleId(map_[i].second) + "\n";
        written[i] = true;
    };
    auto writeRemaining = [&] {
        for (std::size_t i = 0; i < map_.size(); ++i)
            if (!written[i]) entryLine(i);
    };
    auto indexOf = [&](std::string_view key) -> std::ptrdiff_t {
        for (std::size_t i = 0; i < map_.size(); ++i)
            if (map_[i].first == key) return static_cast<std::ptrdiff_t>(i);
        return -1;
    };

    bool inMap = false, sawMap = false;
    while (!original.empty()) {
        const std::size_t nl = original.find('\n');
        std::string_view line = original.substr(0, nl);
        original = nl == std::string_view::npos ? std::string_view{} : original.substr(nl + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const std::string_view t = trim(line);
        if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
            if (inMap) writeRemaining();
            inMap = trim(t.substr(1, t.size() - 2)) == kSection;
            sawMap = sawMap || inMap;
            out.append(line).append("\n");
            continue;
        }
        const std::size_t eq = t.find('=');
        const bool comment = !t.empty() && (t.front() == ';' || t.front() == '#');
        if (!inMap || comment || eq == std::string_view::npos) {
            out.append(line).append("\n");
            continue;
        }
        const std::string_view k = trim(t.substr(0, eq));
        if (k.empty() || !parseTitleId(trim(t.substr(eq + 1)))) {
            out.append(line).append("\n");  // unlesbar: so stehen lassen, wie der Nutzer ihn geschrieben hat
            continue;
        }
        const std::string key = k.find('/') == std::string_view::npos ? std::string(kN64Prefix) + std::string(k) : std::string(k);
        const std::ptrdiff_t i = indexOf(key);
        if (i >= 0 && !written[static_cast<std::size_t>(i)]) entryLine(static_cast<std::size_t>(i));
        // sonst: gelöscht oder doppelt → entfällt
    }
    if (inMap) writeRemaining();
    if (!sawMap && std::find(written.begin(), written.end(), false) != written.end()) {
        out += std::string("[") + kSection + "]\n";
        writeRemaining();
    }
    return out;
}

float vcMatchScore(std::string_view romTitle, std::string_view channelName) {
    const std::vector<u16> a = bigrams(romTitle);
    const std::vector<u16> b = bigrams(channelName);
    if (a.empty() || b.empty()) return 0.0f;
    std::size_t common = 0, i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] < b[j]) ++i;
        else if (b[j] < a[i]) ++j;
        else { ++common; ++i; ++j; }
    }
    return 2.0f * static_cast<float>(common) / static_cast<float>(a.size() + b.size());
}

std::optional<std::size_t> suggestChannel(std::string_view romTitle, std::span<const ChannelInfo> channels) {
    std::optional<std::size_t> best;
    float bestScore = 0.0f;
    for (std::size_t i = 0; i < channels.size(); ++i) {
        const float s = vcMatchScore(romTitle, channels[i].name);
        if (s >= kVcSuggestThreshold && (!best || s > bestScore)) {
            best = i;
            bestScore = s;
        }
    }
    return best;
}

}  // namespace nl
