#include "core/game.hpp"
#include "core/text_fold.hpp"
#include <doctest/doctest.h>
using namespace nl;

TEST_CASE("cleanTitle entfernt No-Intro-Tags") {
    CHECK(cleanTitle("Super Mario 64 (Europe) (En,Fr,De)") == "Super Mario 64");
    CHECK(cleanTitle("Tetris (World) [!]") == "Tetris");
    CHECK(cleanTitle("Zelda (Rev 1) - Link's Awakening") == "Zelda - Link's Awakening");
    CHECK(cleanTitle("(Europe)") == "(Europe)");
    CHECK(cleanTitle("Kirby (unclosed") == "Kirby (unclosed");
    CHECK(cleanTitle("  F-Zero   (Europe)  ") == "F-Zero");
    CHECK(cleanTitle("") == "");
}

TEST_CASE("foldForSearch") {
    CHECK(foldForSearch("Pokémon Snap") == "pokemon snap");
    CHECK(foldForSearch("Die Legende vom Äonentor") == "die legende vom aonentor");
    CHECK(foldForSearch("Straße") == "strasse");
    CHECK(foldForSearch("Mario Kart: Double Dash!!") == "mario kart double dash");
    CHECK(foldForSearch("\xC3") == "");
    CHECK(foldForSearch("ÆØÅ ñ") == "aeoa n");
    CHECK(foldForSearch("  ") == "");
}

TEST_CASE("matchesQuery: Wörter in beliebiger Reihenfolge, leere Anfrage passt immer") {
    CHECK(matchesQuery("super mario 64", "mario"));
    CHECK(matchesQuery("super mario 64", "64 super"));
    CHECK(matchesQuery("super mario 64", ""));
    CHECK_FALSE(matchesQuery("super mario 64", "mario kart"));
    CHECK_FALSE(matchesQuery("", "a"));
}

TEST_CASE("letterBucket") {
    CHECK(letterBucket("Ökoland") == 'O');
    CHECK(letterBucket("1080° Snowboarding") == '#');
    CHECK(letterBucket("") == '#');
    CHECK(letterBucket("banjo") == 'B');
    CHECK(letterBucket("¡Hola") == 'H');  // führende Satzzeichen zählen nicht
}

TEST_CASE("compareTitles: Groß/klein egal, Artikel zählen") {
    CHECK(compareTitles("The Legend", "Super") > 0);
    CHECK(compareTitles("a", "B") < 0);
    CHECK(compareTitles("Zelda", "zelda") != 0);  // Gleichstand wird bytewise entschieden
    CHECK(compareTitles("Mario", "Mario") == 0);
    CHECK(compareTitles("Ökoland", "Pac-Man") < 0);
}

TEST_CASE("firstIndexForBucket") {
    const Game g1{Console::N64, "a", "1080 Snowboarding", 0, 0}, g2{Console::N64, "b", "Banjo", 0, 0},
        g3{Console::N64, "c", "Donkey", 0, 0};
    const Game* sorted[] = {&g1, &g2, &g3};
    CHECK(firstIndexForBucket(sorted, '#') == 0);
    CHECK(firstIndexForBucket(sorted, 'B') == 1);
    CHECK(firstIndexForBucket(sorted, 'C') == 2);  // nächster folgender Buchstabe: D
    CHECK(firstIndexForBucket(sorted, 'Z') == 2);  // nichts danach: letzter Index
    CHECK(firstIndexForBucket(std::span<const Game* const>{}, 'A') == -1);
    const Game z{Console::Nes, "z", "Zelda", 0, 0}, a{Console::Nes, "x", "Arms", 0, 0};
    const Game* unsorted[] = {&z, &a};
    CHECK(firstIndexForBucket(unsorted, 'A') == 1);
    CHECK(firstIndexForBucket(unsorted, 'B') == 0);
}
