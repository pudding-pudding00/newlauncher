// Filter im A–Z-Overlay (M17, Spec 10.3): Alle, Favoriten, Genres, eigene Kategorien.
#include "core/categories.hpp"
#include "core/game_filter.hpp"
#include "core/library_store.hpp"
#include <doctest/doctest.h>
#include <cstring>
using namespace nl;

namespace {
u32 bit(const char* genre) {
    for (std::size_t i = 0; i < kGenres.size(); ++i)
        if (std::strcmp(kGenres[i], genre) == 0) return 1u << i;
    return 0;
}
u8 index(const char* genre) {
    for (std::size_t i = 0; i < kGenres.size(); ++i)
        if (std::strcmp(kGenres[i], genre) == 0) return static_cast<u8>(i);
    return 0;
}
struct Fixture {
    Game games[3] = {
        Game{Console::Wii, "RMCP01", "Mario Kart", 2008, 4},
        Game{Console::Wii, "RSPP01", "Wii Sports", 2006, 4},
        Game{Console::Wii, "SMNP01", "NSMB", 2009, 4},
    };
    std::vector<const Game*> ptrs;
    Fixture() {
        games[0].genres = bit("racing");
        games[1].genres = bit("sports") | bit("general");
        games[2].genres = bit("action") | bit("general");
        for (const Game& g : games) ptrs.push_back(&g);
    }
};
}  // namespace

TEST_CASE("applyFilter: Genre → nur Spiele mit dem Bit, Reihenfolge bleibt") {
    Fixture f;
    const Categories cats;
    auto v = applyFilter(f.ptrs, GameFilter{GameFilter::Genre, index("sports"), {}}, nullptr, cats);
    REQUIRE(v.size() == 1);
    CHECK(v[0] == &f.games[1]);
    v = applyFilter(f.ptrs, GameFilter{GameFilter::Genre, index("general"), {}}, nullptr, cats);
    REQUIRE(v.size() == 2);
    CHECK(v[0] == &f.games[1]);
    CHECK(v[1] == &f.games[2]);
}

TEST_CASE("applyFilter: Kategorie → nur deren Schlüssel; Alle → alles") {
    Fixture f;
    const Categories cats = Categories::fromText("[Party]\nSMNP01\nRMCP01\nXXXX01\n");
    auto v = applyFilter(f.ptrs, GameFilter{GameFilter::Category, 0, "Party"}, nullptr, cats);
    REQUIRE(v.size() == 2);
    CHECK(v[0] == &f.games[0]);
    CHECK(v[1] == &f.games[2]);
    CHECK(applyFilter(f.ptrs, GameFilter{}, nullptr, cats).size() == 3);
    CHECK(applyFilter(f.ptrs, GameFilter{GameFilter::Category, 0, "Fehlt"}, nullptr, cats).empty());
}

TEST_CASE("applyFilter: Favoriten aus dem LibraryStore") {
    Fixture f;
    LibraryStore store;
    store.toggleFavorite("RSPP01");
    auto v = applyFilter(f.ptrs, GameFilter{GameFilter::Favorites, 0, {}}, &store, Categories{});
    REQUIRE(v.size() == 1);
    CHECK(v[0] == &f.games[1]);
    CHECK(applyFilter(f.ptrs, GameFilter{GameFilter::Favorites, 0, {}}, nullptr, Categories{}).empty());
}

TEST_CASE("filterChoices: ohne Genres nur Alle, Favoriten, Kategorien (auch leere)") {
    Game g{Console::Nes, "nes/a", "A", 0, 0};
    const Game* list[] = {&g};
    const Categories cats = Categories::fromText("[Party]\n[Retro]\nnes/a\n");
    const auto c = filterChoices(list, cats);
    REQUIRE(c.size() == 4);
    CHECK(c[0].kind == GameFilter::All);
    CHECK(c[1].kind == GameFilter::Favorites);
    CHECK(c[2].kind == GameFilter::Category);
    CHECK(c[2].category == "Party");
    CHECK(c[3].category == "Retro");
}

TEST_CASE("filterChoices: nur Genres, die in der Liste vorkommen, in kGenres-Reihenfolge") {
    Fixture f;
    const auto c = filterChoices(f.ptrs, Categories{});
    REQUIRE(c.size() == 2 + 4);
    CHECK(c[2].kind == GameFilter::Genre);
    CHECK(c[2].genre == index("general"));
    CHECK(c[3].genre == index("action"));
    CHECK(c[4].genre == index("sports"));
    CHECK(c[5].genre == index("racing"));
}

TEST_CASE("filterSpec und parseFilter für [debug] filter und NL_FILTER") {
    CHECK(filterSpec(GameFilter{}) == "all");
    CHECK(filterSpec(GameFilter{GameFilter::Favorites, 0, {}}) == "favorites");
    CHECK(filterSpec(GameFilter{GameFilter::Genre, index("sports"), {}}) == "genre:sports");
    CHECK(filterSpec(GameFilter{GameFilter::Category, 0, "Party"}) == "category:Party");
    auto p = parseFilter("category:Party");
    REQUIRE(p.has_value());
    CHECK(p->kind == GameFilter::Category);
    CHECK(p->category == "Party");
    p = parseFilter("genre:sports");
    REQUIRE(p.has_value());
    CHECK(p->genre == index("sports"));
    CHECK(parseFilter("favorites")->kind == GameFilter::Favorites);
    CHECK(parseFilter("all")->kind == GameFilter::All);
    CHECK_FALSE(parseFilter("genre:zorbing").has_value());
    CHECK_FALSE(parseFilter("category:").has_value());
    CHECK_FALSE(parseFilter("bunt").has_value());
}
