#include "core/disc_id.hpp"
#include "core/index_file.hpp"
#include "core/play_stats.hpp"

#include <doctest/doctest.h>

#include <cstring>
#include <vector>

using namespace nl;

namespace {

std::vector<u8> makeHeader(std::size_t size, const char* id6, std::size_t magicAt, u32 magic, const char* title) {
    std::vector<u8> h(size, 0);
    std::memcpy(h.data(), id6, 6);
    h[magicAt] = static_cast<u8>(magic >> 24);
    h[magicAt + 1] = static_cast<u8>(magic >> 16);
    h[magicAt + 2] = static_cast<u8>(magic >> 8);
    h[magicAt + 3] = static_cast<u8>(magic);
    if (title != nullptr && size >= 0x60) {
        std::memcpy(h.data() + 0x20, title, std::strlen(title));
    }
    return h;
}

}  // namespace

TEST_CASE("classifyDisc: Wii-Header, GC-Header, Kurz-Header und Unbekannt") {
    const auto wii = makeHeader(0x440, "RMCP01", 0x18, 0x5D1C9EA3u, "Mario Kart Wii");
    const OpticalDisc dWii = classifyDisc(wii);
    CHECK(dWii.kind == DiscKind::Wii);
    CHECK(dWii.id6 == "RMCP01");
    CHECK(dWii.title == "Mario Kart Wii");

    const auto gc = makeHeader(0x440, "GM8P01", 0x1C, 0xC2339F3Du, "Metroid Prime");
    const OpticalDisc dGc = classifyDisc(gc);
    CHECK(dGc.kind == DiscKind::Gc);
    CHECK(dGc.id6 == "GM8P01");
    CHECK(dGc.title == "Metroid Prime");

    // Nur 0x20 Bytes aus READID: Titel fehlt, ID6 und Art stimmen trotzdem.
    const auto wiiShort = makeHeader(0x20, "RMGP01", 0x18, 0x5D1C9EA3u, nullptr);
    const OpticalDisc sWii = classifyDisc(wiiShort);
    CHECK(sWii.kind == DiscKind::Wii);
    CHECK(sWii.id6 == "RMGP01");
    CHECK(sWii.title.empty());

    const auto gcShort = makeHeader(0x20, "GZLJ01", 0x1C, 0xC2339F3Du, nullptr);
    const OpticalDisc sGc = classifyDisc(gcShort);
    CHECK(sGc.kind == DiscKind::Gc);
    CHECK(sGc.id6 == "GZLJ01");

    const std::vector<u8> zeros(0x440, 0);
    CHECK(classifyDisc(zeros).kind == DiscKind::Unknown);
    CHECK(classifyDisc({}).kind == DiscKind::None);
}

TEST_CASE("parseFakeDisc: Wii, gc:ID6 und ungültige Eingaben") {
    const auto wii = parseFakeDisc("RMCP01");
    REQUIRE(wii.has_value());
    CHECK(wii->kind == DiscKind::Wii);
    CHECK(wii->id6 == "RMCP01");

    const auto gc = parseFakeDisc("gc:gm8p01");
    REQUIRE(gc.has_value());
    CHECK(gc->kind == DiscKind::Gc);
    CHECK(gc->id6 == "GM8P01");

    CHECK_FALSE(parseFakeDisc("").has_value());
    CHECK_FALSE(parseFakeDisc("ABC").has_value());
    CHECK_FALSE(parseFakeDisc("gc:123").has_value());
    CHECK_FALSE(parseFakeDisc("wii:RMCP01").has_value());
}

TEST_CASE("sortGames: Disc bleibt bei jeder Sortierart am Anfang der Konsole") {
    Game games[3] = {
        Game{Console::Wii, "RMAP01", "Alpha", 2007, 1, nullptr, "wbfs/RMAP01.wbfs", 0, 0},
        Game{Console::Wii, "RMCP01", "Mario Kart Wii", 2008, 4, nullptr, "disc:", 0, kFlagDisc},
        Game{Console::Wii, "RMGP01", "Zelda", 2006, 1, nullptr, "wbfs/RMGP01.wbfs", 0, 0},
    };
    PlayStats st;
    st.record("RMGP01", 5000);
    st.record("RMGP01", 6000);
    for (SortOrder o : {SortOrder::Title, SortOrder::Year, SortOrder::Recent, SortOrder::Most}) {
        std::vector<const Game*> list{&games[0], &games[1], &games[2]};
        sortGames(list, o, st);
        REQUIRE(list.size() == 3);
        CHECK((list[0]->flags & kFlagDisc) != 0);
        CHECK(std::string(list[0]->key) == "RMCP01");
    }
}
