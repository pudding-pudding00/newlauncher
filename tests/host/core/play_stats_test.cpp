#include "core/play_stats.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <string>
#include <vector>

using namespace nl;

TEST_CASE("PlayStats: record zählt Starts und merkt die letzte Zeit") {
    PlayStats s;
    CHECK(s.get("nes/a").starts == 0);
    s.record("nes/a", 1000);
    s.record("nes/a", 2000);
    CHECK(s.get("nes/a").starts == 2);
    CHECK(s.get("nes/a").last == 2000);
    CHECK(s.get("nes/b").last == 0);
}

TEST_CASE("PlayStats: Text-Rundreise; Schlüssel mit = und ] bleiben erhalten") {
    PlayStats s;
    s.record("nes/Zelda [!] a=b.nes", 1762700000);
    s.record("RMCP01", 5);
    s.record("RMCP01", 6);
    const PlayStats r = PlayStats::fromText(s.toText());
    CHECK(r.get("nes/Zelda [!] a=b.nes").starts == 1);
    CHECK(r.get("nes/Zelda [!] a=b.nes").last == 1762700000);
    CHECK(r.get("RMCP01").starts == 2);
    CHECK(r.get("RMCP01").last == 6);
    CHECK(r.size() == 2);
}

TEST_CASE("PlayStats: kaputte Zeile → nur diese Zeile ignoriert; Zeit in der Zukunft bleibt") {
    const PlayStats s = PlayStats::fromText(
        "; Kommentar\r\n[nes/a]\r\nstarts = abc\r\nlast = 5\r\n"
        "[nes/b]\nstarts = 3\nlast = 4102444800\n"  // 2100-01-01: keine Korrektur
        "[nes/c]\nstarts = 1\nlast = -7\n"
        "[nes/d]\nstarts = 2\n"
        "[nes/e]\nstarts = 12\nlast = abc\n"
        "lose Zeile vor nichts\n[]\nstarts = 9\n");
    CHECK(s.get("nes/a").starts == 0);  // ohne gültiges starts kein Eintrag
    CHECK(s.get("nes/b").starts == 3);
    CHECK(s.get("nes/b").last == 4102444800u);
    CHECK(s.get("nes/c").starts == 1);  // kaputtes last kostet nur die Zeit, nicht die Starts
    CHECK(s.get("nes/c").last == 0);
    CHECK(s.get("nes/d").starts == 2);  // fehlendes last = nie mit Zeit
    CHECK(s.get("nes/d").last == 0);
    CHECK(s.get("nes/e").starts == 12);
    CHECK(s.size() == 4);
}

namespace {
struct SortFixture {
    Game games[4] = {
        Game{Console::Nes, "nes/c", "Gamma", 1990, 0},
        Game{Console::Nes, "nes/a", "Alpha", 0, 0},
        Game{Console::Nes, "nes/b", "Beta", 1985, 0},
        Game{Console::Nes, "nes/d", "Delta", 1990, 0},
    };
    std::vector<const Game*> list() const { return {&games[0], &games[1], &games[2], &games[3]}; }
};
std::string order(const std::vector<const Game*>& v) {
    std::string s;
    for (const Game* g : v) s += g->title[0];
    return s;
}
}  // namespace

TEST_CASE("sortGames: Titel, Jahr (unbekannt zuletzt), Gleichstand nach Titel") {
    SortFixture f;
    const PlayStats none;
    auto v = f.list();
    sortGames(v, SortOrder::Title, none);
    CHECK(order(v) == "ABDG");
    sortGames(v, SortOrder::Year, none);
    CHECK(order(v) == "BDGA");
}

TEST_CASE("sortGames: Recent – nie gespielte am Ende nach Titel; Most – Gleichstand nach Titel") {
    SortFixture f;
    PlayStats s;
    s.record("nes/c", 100);
    s.record("nes/b", 300);
    s.record("nes/b", 400);
    s.record("nes/d", 200);
    auto v = f.list();
    sortGames(v, SortOrder::Recent, s);
    CHECK(order(v) == "BDGA");
    s.record("nes/d", 50);  // Delta 2 Starts wie Beta
    sortGames(v, SortOrder::Most, s);
    CHECK(order(v) == "BDGA");
    PlayStats tie;
    sortGames(v, SortOrder::Most, tie);
    CHECK(order(v) == "ABDG");
}

TEST_CASE("sortOrderName/parseSortOrder") {
    CHECK(std::string(sortOrderName(SortOrder::Most)) == "most");
    CHECK(parseSortOrder("recent") == SortOrder::Recent);
    CHECK(parseSortOrder("year") == SortOrder::Year);
    CHECK_FALSE(parseSortOrder("zufall").has_value());
}

// Der Host-Lauf baut ohne NDEBUG; dieses Limit ist großzügig, fängt aber quadratisches Verhalten
// (5000² Vergleiche) auch ohne Release-Build ab.
TEST_CASE("PlayStats: 5000 Einträge laden < 1 s (jeder Build)") {
    PlayStats s;
    for (u32 i = 0; i < 5000; ++i) s.record("nes/Spiel Nummer " + std::to_string(i) + " (Europe).nes", 1762700000 + i);
    const std::string text = s.toText();
    const auto t0 = std::chrono::steady_clock::now();
    const PlayStats r = PlayStats::fromText(text);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(r.size() == 5000);
    CHECK(ms < 1000);
}

#ifdef NDEBUG
TEST_CASE("PlayStats: 5000 Einträge laden < 100 ms (Release)") {
    PlayStats s;
    for (u32 i = 0; i < 5000; ++i) s.record("nes/Spiel Nummer " + std::to_string(i) + " (Europe).nes", 1762700000 + i);
    const std::string text = s.toText();
    const auto t0 = std::chrono::steady_clock::now();
    const PlayStats r = PlayStats::fromText(text);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(r.size() == 5000);
    CHECK(ms < 100);
}
#endif
