#include "core/play_stats.hpp"

#include <algorithm>
#include <charconv>
#include <limits>

#include "core/index_file.hpp"
#include "core/ini.hpp"
#include "core/text_fold.hpp"

namespace nl {

namespace {
std::optional<u32> parseU32(std::string_view s) {
    s = trim(s);
    if (s.empty()) return std::nullopt;
    u32 v = 0;
    const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || p != s.data() + s.size()) return std::nullopt;
    return v;
}

struct Pending {
    std::string key;
    std::optional<u32> starts;
    std::optional<u32> last;
};

void commit(std::map<std::string, PlayStat, std::less<>>& out, const Pending& p) {
    if (p.key.empty() || !p.starts || *p.starts == 0) return;
    out[p.key] = PlayStat{*p.starts, p.last.value_or(0)};
}

bool ciEqual(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const char x = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
        if (x != b[i]) return false;
    }
    return true;
}
}  // namespace

PlayStats PlayStats::fromText(std::string_view text) {
    PlayStats r;
    Pending cur;
    bool inSection = false;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const std::size_t end = nl == std::string_view::npos ? text.size() : nl;
        const std::string_view line = trim(text.substr(pos, end - pos));
        pos = nl == std::string_view::npos ? text.size() : nl + 1;
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.front() == '[' && line.back() == ']' && line.size() >= 2) {
            if (inSection) commit(r.stats_, cur);
            cur = Pending{};
            cur.key.assign(trim(line.substr(1, line.size() - 2)));
            inSection = true;
            continue;
        }
        if (!inSection) continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string_view name = trim(line.substr(0, eq));
        const std::optional<u32> value = parseU32(line.substr(eq + 1));
        if (!value) continue;  // kaputte Zeile: nur sie fällt weg, nicht der ganze Eintrag
        if (name == "starts") cur.starts = value;
        else if (name == "last") cur.last = value;
    }
    if (inSection) commit(r.stats_, cur);
    return r;
}

std::string PlayStats::toText() const {
    std::string out = "; newlauncher Spielstatistik (M17)\n";
    for (const auto& [key, st] : stats_) {
        out += '[';
        out += key;
        out += "]\nstarts = ";
        out += std::to_string(st.starts);
        out += "\nlast = ";
        out += std::to_string(st.last);
        out += '\n';
    }
    return out;
}

PlayStat PlayStats::get(std::string_view key) const {
    const auto it = stats_.find(key);
    return it != stats_.end() ? it->second : PlayStat{};
}

void PlayStats::record(std::string_view key, u32 now) {
    if (key.empty()) return;
    auto it = stats_.find(key);
    if (it == stats_.end()) it = stats_.emplace(std::string(key), PlayStat{}).first;
    if (it->second.starts < std::numeric_limits<u32>::max()) ++it->second.starts;
    it->second.last = now;
}

void sortGames(std::span<const Game*> v, SortOrder o, const PlayStats& s) {
    const auto byTitle = [](const Game* a, const Game* b) { return compareTitles(a->title, b->title) < 0; };
    const auto isDisc = [](const Game* g) { return (g->flags & kFlagDisc) != 0; };
    const auto byOrder = [&](auto&& cmp) {
        std::stable_sort(v.begin(), v.end(), [&](const Game* a, const Game* b) {
            if (isDisc(a) != isDisc(b)) return isDisc(a);
            return cmp(a, b);
        });
    };
    switch (o) {
        case SortOrder::Title:
            byOrder(byTitle);
            return;
        case SortOrder::Year:
            byOrder([&](const Game* a, const Game* b) {
                const u32 ya = a->year != 0 ? a->year : 0x10000u, yb = b->year != 0 ? b->year : 0x10000u;
                return ya != yb ? ya < yb : byTitle(a, b);
            });
            return;
        case SortOrder::Recent:
            byOrder([&](const Game* a, const Game* b) {
                const PlayStat sa = s.get(a->key), sb = s.get(b->key);
                // nie gestartet (starts == 0) ans Ende, auch wenn last gesetzt wäre
                const u64 la = sa.starts != 0 ? u64{sa.last} + 1 : 0, lb = sb.starts != 0 ? u64{sb.last} + 1 : 0;
                return la != lb ? la > lb : byTitle(a, b);
            });
            return;
        case SortOrder::Most:
            byOrder([&](const Game* a, const Game* b) {
                const u32 na = s.get(a->key).starts, nb = s.get(b->key).starts;
                return na != nb ? na > nb : byTitle(a, b);
            });
            return;
    }
}

SortOrder toggledSort(SortOrder o) { return o == SortOrder::Title ? SortOrder::Year : SortOrder::Title; }

const char* sortOrderName(SortOrder o) {
    switch (o) {
        case SortOrder::Year: return "year";
        case SortOrder::Recent: return "recent";
        case SortOrder::Most: return "most";
        case SortOrder::Title: break;
    }
    return "title";
}

std::optional<SortOrder> parseSortOrder(std::string_view s) {
    s = trim(s);
    for (SortOrder o : {SortOrder::Title, SortOrder::Year, SortOrder::Recent, SortOrder::Most})
        if (ciEqual(s, sortOrderName(o))) return o;
    return std::nullopt;
}

}  // namespace nl
