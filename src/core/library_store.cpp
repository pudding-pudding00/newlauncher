#include "core/library_store.hpp"

#include <algorithm>

#include "core/vc_map.hpp"

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
    MergedLibrary lib;
    lib.devices.emplace_back();
    lib.games.reserve(index.games.size());
    for (IndexGame& g : index.games) lib.games.push_back(MergedGame{std::move(g), 0, {}});
    setGames(std::move(lib), meta);
}

std::span<const u8> LibraryStore::alsoOn(const Game& g) const {
    if ((g.flags & kFlagDisc) != 0 || g.device == kNoDevice) return {};
    auto it = fileIndexByKey_.find(g.key != nullptr ? std::string_view(g.key) : std::string_view());
    if (it == fileIndexByKey_.end()) return {};
    return lib_.games[it->second].alsoOn;
}

void LibraryStore::setGames(MergedLibrary library, const MetaDb* meta) {
    lib_ = std::move(library);
    descriptions_.clear();
    fileGames_.clear();
    fileGames_.reserve(lib_.games.size());
    fileIndexByKey_.clear();
    for (std::size_t i = 0; i < lib_.games.size(); ++i) {
        const MergedGame& m = lib_.games[i];
        const IndexGame& g = m.game;
        Game game{g.console, g.key.c_str(), g.title.c_str(), 0, 0};
        game.path = g.path.c_str();
        game.size = g.size;
        game.flags = g.flags;
        game.device = m.device;
        if (meta != nullptr) {
            if (auto e = meta->find(metaKey(g))) {
                game.year = e->year;
                game.players = e->players;
                game.genres = e->genres;
                game.rating = e->rating;
                if (!e->description.empty()) {
                    descriptions_.emplace_back(e->description);
                    game.description = descriptions_.back().c_str();
                }
            }
        }
        fileGames_.push_back(game);
        fileIndexByKey_.emplace(lib_.games[i].game.key, i);
    }
    if (disc_) applyDiscMeta(meta);
    rebuildView();
}

void LibraryStore::applyDiscMeta(const MetaDb* meta) {
    if (!disc_) return;
    discKey_ = disc_->id6;
    discTitle_ = disc_->title.empty() ? disc_->id6 : disc_->title;
    discPath_ = "disc:";
    discDesc_.clear();
    discYear_ = 0;
    discPlayers_ = 0;
    discGenres_ = 0;
    discRating_ = {};
    if (meta != nullptr) {
        if (auto e = meta->find(discKey_)) {
            if (!e->title.empty()) discTitle_ = std::string(e->title);
            discYear_ = e->year;
            discPlayers_ = e->players;
            discGenres_ = e->genres;
            discRating_ = e->rating;
            if (!e->description.empty()) discDesc_ = std::string(e->description);
        }
    }
}

void LibraryStore::setDisc(std::optional<OpticalDisc> d, const MetaDb* meta) {
    if (!d || (d->kind != DiscKind::Wii && d->kind != DiscKind::Gc) || d->id6.size() != 6) {
        disc_.reset();
        discKey_.clear();
        discTitle_.clear();
        discPath_.clear();
        discDesc_.clear();
        rebuildView();
        return;
    }
    disc_ = std::move(d);
    applyDiscMeta(meta);
    rebuildView();
}

void LibraryStore::setChannels(std::vector<EmuTitle> channels) {
    channels_.clear();
    channels_.reserve(channels.size());
    for (EmuTitle& t : channels) {
        channels_.push_back(StoredChannel{formatTitleId(t.titleId), std::move(t.name), std::move(t.content0Path)});
    }
    rebuildView();
}

void LibraryStore::rebuildView() {
    games_.clear();
    games_.reserve(fileGames_.size() + channels_.size() + (disc_ ? 1u : 0u));
    bool discInserted = false;
    Game discGame{};
    if (disc_) {
        const Console c = disc_->kind == DiscKind::Gc ? Console::Gc : Console::Wii;
        discGame = Game{c,
                        discKey_.c_str(),
                        discTitle_.c_str(),
                        discYear_,
                        discPlayers_,
                        discDesc_.empty() ? nullptr : discDesc_.c_str(),
                        discPath_.c_str(),
                        0,
                        kFlagDisc,
                        kNoDevice,
                        discGenres_,
                        discRating_};
    }
    for (const Game& fg : fileGames_) {
        if (disc_ && !discInserted && fg.console == discGame.console) {
            games_.push_back(discGame);
            discInserted = true;
        }
        games_.push_back(fg);
    }
    if (disc_ && !discInserted) games_.push_back(discGame);
    for (const StoredChannel& ch : channels_) {
        games_.push_back(Game{Console::Channel, ch.key.c_str(), ch.title.c_str(), 0, 0, nullptr, ch.path.c_str(), 0, 0,
                              kNoDevice});
    }

    byKey_.clear();
    for (std::size_t i = 0; i < games_.size(); ++i) {
        if (games_[i].key != nullptr) byKey_.emplace(games_[i].key, i);
    }
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

void LibraryStore::recordPlayed(std::string_view key, u32 unixTime) {
    auto it = std::find_if(history_.begin(), history_.end(), [&](const auto& h) { return h.second == key; });
    if (it != history_.end()) history_.erase(it);
    history_.emplace(history_.begin(), unixTime, std::string(key));
    if (history_.size() > static_cast<std::size_t>(kHistoryMax)) history_.resize(kHistoryMax);
    rebuildLists();
}

IniDoc LibraryStore::historyIni() const {
    // Über den Text statt set(), weil set() gleiche Zeitstempel (zwei Starts in einer Sekunde) zusammenlegt.
    std::string text = "[history]\n";
    for (const auto& [t, k] : history_) text += std::to_string(t) + " = " + k + "\n";
    return IniDoc::parse(text);
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
