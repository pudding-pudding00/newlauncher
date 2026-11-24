#pragma once
// Bibliotheks-Speicher: besitzt die Spiele-Strings und stellt stabile Game-Sichten bereit,
// dazu Favoriten (favorites.ini) und Verlauf (history.ini), Spec 5.1/5.4.

#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/disc_id.hpp"
#include "core/emunand.hpp"
#include "core/game.hpp"
#include "core/index_file.hpp"
#include "core/ini.hpp"
#include "core/library_merge.hpp"
#include "core/meta_db.hpp"

namespace nl {

inline constexpr int kHistoryMax = 20;

class LibraryStore {
public:
    // Übernimmt die Spiele; alle Game-Zeiger bleiben bis zum nächsten setGames/setDisc gültig.
    void setGames(MergedLibrary library, const MetaDb* meta);
    void setGames(IndexData index, const MetaDb* meta);  // ein Gerät ohne Namen (Tests, Altpfade)
    // Eingelegte Disc (M19): steht in ihrer Konsole vor allen anderen Spielen; nullopt entfernt sie.
    void setDisc(std::optional<OpticalDisc> d, const MetaDb* meta = nullptr);
    // EmuNAND-Kanäle (M19): erscheinen unter Console::Channel nach allen Datei-Spielen.
    void setChannels(std::vector<EmuTitle> channels);
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
    void applyDiscMeta(const MetaDb* meta);
    void rebuildView();
    void rebuildLists();

    MergedLibrary lib_;
    std::deque<std::string> descriptions_;
    std::vector<Game> fileGames_;
    std::unordered_map<std::string_view, std::size_t> fileIndexByKey_;
    std::optional<OpticalDisc> disc_;
    std::string discKey_, discTitle_, discPath_, discDesc_;
    u16 discYear_ = 0;
    u8 discPlayers_ = 0;
    u32 discGenres_ = 0;
    Rating discRating_{};
    struct StoredChannel {
        std::string key;
        std::string title;
        std::string path;
    };
    std::vector<StoredChannel> channels_;
    std::vector<Game> games_;
    std::unordered_map<std::string_view, std::size_t> byKey_;
    std::vector<std::string> favoriteKeys_;
    std::vector<std::pair<u32, std::string>> history_;  // neueste zuerst
    std::vector<const Game*> favoriteGames_, recentGames_;
};

}  // namespace nl
