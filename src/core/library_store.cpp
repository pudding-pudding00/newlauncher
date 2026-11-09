#include "core/library_store.hpp"

#include <algorithm>

namespace nl {

namespace {
bool parseUnixTime(std::string_view s, u32& out) {
    if (s.empty() || s.size() > 10) return false;
    u64 v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<u64>(c - '0');
    }
    if (v > 0xFFFFFFFFull) return false;
    out = static_cast<u32>(v);
    return true;
}
}  // namespace

void LibraryStore::setGames(IndexData index, const MetaDb* meta) {
    index_ = std::move(index);
    descriptions_.clear();
    games_.clear();
    games_.reserve(index_.games.size());
    byKey_.clear();
    for (const IndexGame& g : index_.games) {
        Game game{g.console, g.key.c_str(), g.title.c_str(), 0, 0};
        game.path = g.path.c_str();
        game.size = g.size;
        game.flags = g.flags;
        if (meta != nullptr) {
            if (auto e = meta->find(metaKey(g))) {
                game.year = e->year;
                game.players = e->players;
                if (!e->description.empty()) {
                    descriptions_.emplace_back(e->description);
                    game.description = descriptions_.back().c_str();
                }
            }
        }
        games_.push_back(game);
    }
    for (std::size_t i = 0; i < index_.games.size(); ++i) byKey_.emplace(index_.games[i].key, i);
    rebuildLists();
}

const Game* LibraryStore::findByKey(std::string_view key) const {
    auto it = byKey_.find(key);
    return it == byKey_.end() ? nullptr : &games_[it->second];
}

void LibraryStore::loadFavorites(const IniDoc& ini) {
    favoriteKeys_.clear();
    for (const std::string& line : ini.rawLines("favorites"))
        if (!isFavorite(line)) favoriteKeys_.push_back(line);
    rebuildLists();
}

IniDoc LibraryStore::favoritesIni() const {
    IniDoc d;
    for (const std::string& k : favoriteKeys_) d.addLine("favorites", k);
    return d;
}

bool LibraryStore::isFavorite(std::string_view key) const {
    return std::find(favoriteKeys_.begin(), favoriteKeys_.end(), key) != favoriteKeys_.end();
}

bool LibraryStore::toggleFavorite(std::string_view key) {
    auto it = std::find(favoriteKeys_.begin(), favoriteKeys_.end(), key);
    const bool nowFavorite = it == favoriteKeys_.end();
    if (nowFavorite) favoriteKeys_.emplace_back(key);
    else favoriteKeys_.erase(it);
    rebuildLists();
    return nowFavorite;
}

void LibraryStore::loadHistory(const IniDoc& ini) {
    history_.clear();
    for (const auto& [time, key] : ini.entries("history")) {
        u32 t = 0;
        if (key.empty() || !parseUnixTime(time, t)) continue;
        auto it = std::find_if(history_.begin(), history_.end(), [&](const auto& h) { return h.second == key; });
        if (it == history_.end()) history_.emplace_back(t, key);
        else if (t > it->first) it->first = t;
    }
    std::stable_sort(history_.begin(), history_.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if (history_.size() > static_cast<std::size_t>(kHistoryMax)) history_.resize(kHistoryMax);
    rebuildLists();
}

u32 LibraryStore::lastPlayed(std::string_view key) const {
    for (const auto& [t, k] : history_)
        if (k == key) return t;
    return 0;
}

void LibraryStore::rebuildLists() {
    favoriteGames_.clear();
    for (const std::string& k : favoriteKeys_)
        if (const Game* g = findByKey(k)) favoriteGames_.push_back(g);
    recentGames_.clear();
    for (const auto& h : history_)
        if (const Game* g = findByKey(h.second)) recentGames_.push_back(g);
}

}  // namespace nl
