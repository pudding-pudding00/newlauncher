// meta.bin v2 (M17, Spec 10.1/10.5): Genres als Bitmaske und Altersfreigabe je Eintrag.
#include "core/bin_io.hpp"
#include "core/meta_db.hpp"
#include <doctest/doctest.h>
#include <cstring>
using namespace nl;

namespace {
std::vector<u8> metaV(u16 version, u32 genres, u8 system, u8 age) {
    BeWriter w;
    for (char c : std::string_view("NLMD")) w.u8(static_cast<u8>(c));
    w.u16(version); w.u16(0); w.u32(2);
    for (const char* k : {"RMCP01", "nes/a"}) {
        w.str(k); w.str("Titel"); w.u16(2008); w.u8(4); w.str("Text");
        if (version >= 2) { w.u32(genres); w.u8(system); w.u8(age); }
    }
    return w.bytes();
}
int genreIndex(const char* name) {
    for (std::size_t i = 0; i < kGenres.size(); ++i)
        if (std::strcmp(kGenres[i], name) == 0) return static_cast<int>(i);
    return -1;
}
}  // namespace

TEST_CASE("meta.bin v1: keine Genres, keine Bewertung") {
    auto db = MetaDb::parse(metaV(1, 0, 0, 0));
    REQUIRE(db.isOk());
    auto e = db.value().find("nes/a");
    REQUIRE(e.has_value());
    CHECK(e->genres == 0);
    CHECK(e->rating.system == RatingSystem::None);
    CHECK(e->rating.age == 0);
    CHECK(e->title == "Titel");
}

TEST_CASE("meta.bin v2: zwei Genres und PEGI 12") {
    const int sports = genreIndex("sports"), music = genreIndex("music");
    REQUIRE(sports >= 0);
    REQUIRE(music >= 0);
    const u32 mask = (1u << sports) | (1u << music);
    auto db = MetaDb::parse(metaV(2, mask, static_cast<u8>(RatingSystem::Pegi), 12));
    REQUIRE(db.isOk());
    CHECK(db.value().size() == 2);
    auto e = db.value().find("RMCP01");
    REQUIRE(e.has_value());
    CHECK(e->genres == mask);
    CHECK(e->rating.system == RatingSystem::Pegi);
    CHECK(e->rating.age == 12);
    CHECK(e->players == 4);
    CHECK(e->description == "Text");
}

TEST_CASE("meta.bin v3 und abgeschnittenes v2 → InvalidData") {
    auto v3 = MetaDb::parse(metaV(3, 0, 0, 0));
    REQUIRE_FALSE(v3.isOk());
    CHECK(v3.error().code == ErrorCode::InvalidData);
    const std::vector<u8> good = metaV(2, 1, 2, 12);
    for (std::size_t n = 0; n < good.size(); ++n)
        CHECK_FALSE(MetaDb::parse(std::vector<u8>(good.begin(), good.begin() + static_cast<long>(n))).isOk());
}

TEST_CASE("meta.bin v2: unbekanntes Bewertungssystem → keine Bewertung") {
    auto db = MetaDb::parse(metaV(2, 0, 9, 18));
    REQUIRE(db.isOk());
    CHECK(db.value().find("nes/a")->rating.system == RatingSystem::None);
}

TEST_CASE("kGenres: GameTDB-Hauptgenres in fester Reihenfolge, passen in u32") {
    CHECK(kGenres.size() <= 32);
    CHECK(std::strcmp(kGenres[0], "theme") == 0);
    CHECK(std::strcmp(kGenres[2], "action") == 0);
    CHECK(std::strcmp(kGenres[kGenres.size() - 1], "others") == 0);
}
