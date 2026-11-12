#pragma once
// Bibliotheks-Speicher: besitzt die Spiele-Strings und stellt stabile Game-Sichten bereit,
// dazu Favoriten (favorites.ini) und Verlauf (history.ini), Spec 5.1/5.4.

#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/game.hpp"
#include "core/index_file.hpp"
#include "core/ini.hpp"
#include "core/library_merge.hpp"
#include "core/meta_db.hpp"

namespace nl {

inline constexpr int kHistoryMax = 20;

class LibraryStore {
public:
    // Übernimmt die Spiele; alle Game-Zeiger bleiben bis zum nächsten setGames gültig.
    void setGames(MergedLibrary library, const MetaDb* meta);
    void setGames(IndexData index, const MetaDb* meta);  // ein Gerät ohne Namen (Tests, Altpfade)
    std::span<const Game> games() const { return games_; }
    const Game* findByKey(std::string_view key) const;
    const MergedLibrary& library() const { return lib_; }
    std::span<const std::string> devices() const { return lib_.devices; }
    std::span<const u8> alsoOn(const Game& g) const;  // weitere Geräte mit demselben Spiel

    void loadFavorites(const IniDoc& ini);  // [favorites], eine Zeile pro Schlüssel
    IniDoc favoritesIni() const;            // unbekannte Schlüssel bleiben erhalten
    bool isFavorite(std::string_view key) const;
    bool toggleFavorite(std::string_view key);  // liefert den neuen Zustand

    void loadHistory(const IniDoc& ini);  // [history] <unixzeit> = <schlüssel>
    u32 lastPlayed(std::string_view key) const;
    void recordPlayed(std::string_view key, u32 unixTime);  // an den Anfang, Duplikat raus, max. kHistoryMax
    IniDoc historyIni() const;                               // neueste zuerst

    std::span<const Game* const> favorites() const { return favoriteGames_; }
    std::span<const Game* const> recent() const { return recentGames_; }

private:
    void rebuildLists();

    MergedLibrary lib_;
    std::deque<std::string> descriptions_;
    std::vector<Game> games_;
    std::unordered_map<std::string_view, std::size_t> byKey_;
    std::vector<std::string> favoriteKeys_;
    std::vector<std::pair<u32, std::string>> history_;  // neueste zuerst
    std::vector<const Game*> favoriteGames_, recentGames_;
};

}  // namespace nl
