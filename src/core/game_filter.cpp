#include "core/game_filter.hpp"

#include "core/library_store.hpp"

namespace nl {

namespace {
bool matches(const Game& g, const GameFilter& f, const LibraryStore* lib, const Categories& cats) {
    switch (f.kind) {
    case GameFilter::All: return true;
    case GameFilter::Favorites: return lib != nullptr && g.key != nullptr && lib->isFavorite(g.key);
    case GameFilter::Genre: return f.genre < kGenres.size() && (g.genres & (1u << f.genre)) != 0;
    case GameFilter::Category: return g.key != nullptr && cats.contains(f.category, g.key);
    }
    return false;
}

constexpr std::string_view kGenrePrefix = "genre:", kCategoryPrefix = "category:";
}  // namespace

std::vector<const Game*> applyFilter(std::span<const Game* const> games, const GameFilter& f, const LibraryStore* lib,
                                     const Categories& cats) {
    std::vector<const Game*> out;
    out.reserve(games.size());
    for (const Game* g : games)
        if (g != nullptr && matches(*g, f, lib, cats)) out.push_back(g);
    return out;
}

std::vector<GameFilter> filterChoices(std::span<const Game* const> games, const Categories& cats) {
    std::vector<GameFilter> out{GameFilter{}, GameFilter{GameFilter::Favorites, 0, {}}};
    u32 present = 0;
    for (const Game* g : games)
        if (g != nullptr) present |= g->genres;
    for (std::size_t i = 0; i < kGenres.size(); ++i)
        if ((present & (1u << i)) != 0) out.push_back(GameFilter{GameFilter::Genre, static_cast<u8>(i), {}});
    for (const std::string& name : cats.names()) out.push_back(GameFilter{GameFilter::Category, 0, name});
    return out;
}

std::string filterSpec(const GameFilter& f) {
    switch (f.kind) {
    case GameFilter::All: return "all";
    case GameFilter::Favorites: return "favorites";
    case GameFilter::Genre:
        return std::string(kGenrePrefix) + (f.genre < kGenres.size() ? kGenres[f.genre] : "?");
    case GameFilter::Category: return std::string(kCategoryPrefix) + f.category;
    }
    return "all";
}

std::optional<GameFilter> parseFilter(std::string_view spec) {
    if (spec == "all") return GameFilter{};
    if (spec == "favorites") return GameFilter{GameFilter::Favorites, 0, {}};
    if (spec.starts_with(kGenrePrefix)) {
        const std::string_view name = spec.substr(kGenrePrefix.size());
        for (std::size_t i = 0; i < kGenres.size(); ++i)
            if (name == kGenres[i]) return GameFilter{GameFilter::Genre, static_cast<u8>(i), {}};
        return std::nullopt;
    }
    if (spec.starts_with(kCategoryPrefix) && spec.size() > kCategoryPrefix.size())
        return GameFilter{GameFilter::Category, 0, std::string(spec.substr(kCategoryPrefix.size()))};
    return std::nullopt;
}

}  // namespace nl
