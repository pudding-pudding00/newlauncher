#pragma once
// Filter im A–Z-Overlay (M17, Spec 10.3): Alle, Favoriten, Genres, eigene Kategorien. Gilt für die aktuelle
// Leiste (Konsole) und wird nicht gespeichert.

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/categories.hpp"
#include "core/game.hpp"

namespace nl {

class LibraryStore;

struct GameFilter {
    enum Kind : u8 { All, Favorites, Genre, Category } kind = All;
    u8 genre = 0;          // Index in kGenres
    std::string category;  // Name aus categories.ini
    bool operator==(const GameFilter&) const = default;
};

// Reihenfolge bleibt; lib == nullptr → keine Favoriten.
std::vector<const Game*> applyFilter(std::span<const Game* const> games, const GameFilter& f, const LibraryStore* lib,
                                     const Categories& cats);
// Alle, Favoriten, dann die Genres, die in games vorkommen (kGenres-Reihenfolge), dann alle Kategorien.
std::vector<GameFilter> filterChoices(std::span<const Game* const> games, const Categories& cats);

// "all", "favorites", "genre:<kGenres-Name>", "category:<Name>" ([debug] filter, Log NL_FILTER).
std::string filterSpec(const GameFilter& f);
std::optional<GameFilter> parseFilter(std::string_view spec);

}  // namespace nl
