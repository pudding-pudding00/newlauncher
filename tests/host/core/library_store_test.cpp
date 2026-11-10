#include "core/bin_io.hpp"
#include "core/library_store.hpp"
#include <doctest/doctest.h>
#include <cstring>
#include <string>
using namespace nl;

namespace {
IndexData lib(std::initializer_list<const char*> keys) {
    IndexData d;
    for (const char* k : keys) d.games.push_back(IndexGame{Console::Nes, 0, k, std::string("T ") + k, std::string("roms/") + k, 1});
    return d;
}

MetaDb meta() {
    BeWriter w;
    for (char c : std::string_view("NLMD")) w.u8(static_cast<u8>(c));
    w.u16(1); w.u16(0); w.u32(1);
    w.str("nes/a"); w.str("Titel A"); w.u16(1985); w.u8(2); w.str("Beschreibung A");
    auto db = MetaDb::parse(w.bytes());
    REQUIRE(db.isOk());
    return std::move(db).value();
}

std::vector<std::string> keysOf(std::span<const Game* const> games) {
    std::vector<std::string> out;
    for (const Game* g : games) out.emplace_back(g->key);
    return out;
}
}  // namespace

TEST_CASE("setGames baut Game-Sichten mit Meta-Feldern") {
    LibraryStore s;
    const MetaDb m = meta();
    s.setGames(lib({"nes/a.nes", "nes/b.nes"}), &m);
    REQUIRE(s.games().size() == 2);
    const Game& a = s.games()[0];
    CHECK(std::strcmp(a.key, "nes/a.nes") == 0);
    CHECK(std::strcmp(a.title, "T nes/a.nes") == 0);
    CHECK(a.year == 1985);
    CHECK(a.players == 2);
    REQUIRE(a.description != nullptr);
    CHECK(std::strcmp(a.description, "Beschreibung A") == 0);
    CHECK(std::strcmp(a.path, "roms/nes/a.nes") == 0);
    CHECK(a.size == 1);
    CHECK(s.games()[1].description == nullptr);
    CHECK(s.findByKey("nes/b.nes") == &s.games()[1]);
    CHECK(s.findByKey("fehlt") == nullptr);
}

TEST_CASE("Favoriten: Dateireihenfolge, unbekannte Schlüssel bleiben in der Datei, erscheinen aber nicht") {
    LibraryStore s;
    s.setGames(lib({"nes/a.nes", "nes/b.nes"}), nullptr);
    s.loadFavorites(IniDoc::parse("[favorites]\nnes/b.nes\nn64/gibts nicht.z64\nnes/a.nes\n"));
    CHECK(keysOf(s.favorites()) == std::vector<std::string>{"nes/b.nes", "nes/a.nes"});
    const std::string out = s.favoritesIni().serialize();
    CHECK(out.find("n64/gibts nicht.z64") != std::string::npos);
    CHECK(s.isFavorite("nes/b.nes"));
    CHECK_FALSE(s.isFavorite("nes/c.nes"));
}

TEST_CASE("toggleFavorite hängt an und entfernt; Round-Trip") {
    LibraryStore s;
    s.setGames(lib({"nes/a.nes", "nes/b.nes"}), nullptr);
    CHECK(s.toggleFavorite("nes/b.nes"));
    CHECK(s.toggleFavorite("nes/a.nes"));
    CHECK(keysOf(s.favorites()) == std::vector<std::string>{"nes/b.nes", "nes/a.nes"});
    CHECK_FALSE(s.toggleFavorite("nes/b.nes"));
    LibraryStore t;
    t.setGames(lib({"nes/a.nes", "nes/b.nes"}), nullptr);
    t.loadFavorites(IniDoc::parse(s.favoritesIni().serialize()));
    CHECK(keysOf(t.favorites()) == std::vector<std::string>{"nes/a.nes"});
}

TEST_CASE("Schlüssel mit ; = [ ] Umlauten überstehen den Round-Trip") {
    const char* key = "nes/Mario; Luigi = Bros [!] Ä.nes";
    LibraryStore s;
    s.setGames(lib({key}), nullptr);
    s.toggleFavorite(key);
    LibraryStore t;
    t.setGames(lib({key}), nullptr);
    t.loadFavorites(IniDoc::parse(s.favoritesIni().serialize()));
    CHECK(t.isFavorite(key));
    REQUIRE(t.favorites().size() == 1);
    t.loadHistory(IniDoc::parse(std::string("[history]\n1700000000 = ") + key + "\n"));
    CHECK(t.lastPlayed(key) == 1700000000u);
}

TEST_CASE("Verlauf: neueste zuerst, doppelte Schlüssel einmal, max 20, kaputte Zeitstempel ignoriert") {
    std::vector<std::string> keys;
    std::string ini = "[history]\n";
    for (int i = 0; i < 25; ++i) {
        keys.push_back("nes/" + std::to_string(i) + ".nes");
        ini += std::to_string(1000 + i) + " = " + keys.back() + "\n";
    }
    ini += "2000 = nes/3.nes\nabc = nes/4.nes\n = nes/5.nes\n";
    IndexData d;
    for (const auto& k : keys) d.games.push_back(IndexGame{Console::Nes, 0, k, k, k, 1});
    LibraryStore s;
    s.setGames(std::move(d), nullptr);
    s.loadHistory(IniDoc::parse(ini));
    const auto recent = s.recent();
    REQUIRE(recent.size() == kHistoryMax);
    CHECK(std::strcmp(recent[0]->key, "nes/3.nes") == 0);   // 2000 ist der neueste Zeitpunkt
    CHECK(std::strcmp(recent[1]->key, "nes/24.nes") == 0);
    CHECK(s.lastPlayed("nes/3.nes") == 2000u);
    CHECK(s.lastPlayed("nes/fehlt.nes") == 0u);
}

TEST_CASE("favorites() und recent() folgen setGames") {
    LibraryStore s;
    s.setGames(lib({"nes/a.nes", "nes/b.nes"}), nullptr);
    s.toggleFavorite("nes/a.nes");
    s.loadHistory(IniDoc::parse("[history]\n5 = nes/a.nes\n"));
    CHECK(s.favorites().size() == 1);
    CHECK(s.recent().size() == 1);
    s.setGames(lib({"nes/b.nes"}), nullptr);
    CHECK(s.favorites().empty());
    CHECK(s.recent().empty());
    CHECK(s.isFavorite("nes/a.nes"));  // bleibt in der Datei
    s.setGames(lib({"nes/a.nes"}), nullptr);
    CHECK(s.favorites().size() == 1);
}

TEST_CASE("recordPlayed setzt an den Anfang, entfernt Duplikat, kappt bei 20, historyIni round-trip") {
    std::vector<std::string> keys;
    IndexData d;
    for (int i = 0; i < 22; ++i) {
        keys.push_back("nes/" + std::to_string(i) + ".nes");
        d.games.push_back(IndexGame{Console::Nes, 0, keys.back(), keys.back(), keys.back(), 1});
    }
    LibraryStore s;
    s.setGames(std::move(d), nullptr);
    for (int i = 0; i < 22; ++i) s.recordPlayed(keys[i], 1000);  // gleiche Sekunde: keiner darf verloren gehen
    REQUIRE(s.recent().size() == kHistoryMax);
    CHECK(std::strcmp(s.recent()[0]->key, "nes/21.nes") == 0);
    s.recordPlayed("nes/5.nes", 2000);
    REQUIRE(s.recent().size() == kHistoryMax);
    CHECK(std::strcmp(s.recent()[0]->key, "nes/5.nes") == 0);
    CHECK(std::strcmp(s.recent()[1]->key, "nes/21.nes") == 0);
    CHECK(s.lastPlayed("nes/5.nes") == 2000u);
    CHECK(s.lastPlayed("nes/0.nes") == 0u);  // aus dem Verlauf gefallen

    LibraryStore t;
    t.setGames(s.index(), nullptr);
    t.loadHistory(IniDoc::parse(s.historyIni().serialize()));
    REQUIRE(t.recent().size() == kHistoryMax);
    CHECK(std::strcmp(t.recent()[0]->key, "nes/5.nes") == 0);
    CHECK(t.lastPlayed("nes/5.nes") == 2000u);
    CHECK(t.lastPlayed("nes/21.nes") == 1000u);
}
