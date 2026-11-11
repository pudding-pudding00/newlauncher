#include "core/wbfs_map.hpp"
#include <doctest/doctest.h>

#include <cstring>
using namespace nl;

namespace {
// Kopf einer .wbfs-Datei: Sektor 0 = wbfs_head_t, ab 512 die Disc-Info (0x100 Kopie des Disc-Headers,
// dann die wlba-Tabelle in Big-Endian).
std::vector<u8> head(u8 shift, std::initializer_list<std::pair<u32, u16>> blocks) {
    std::vector<u8> h(wbfsHeadBytes(shift), 0);
    std::memcpy(h.data(), "WBFS", 4);
    h[8] = 9;
    h[9] = shift;
    for (auto [i, w] : blocks) {
        h[512 + 0x100 + 2 * i] = u8(w >> 8);
        h[512 + 0x100 + 2 * i + 1] = u8(w);
    }
    return h;
}
}  // namespace

TEST_CASE("wbfs_map: Magic falsch") {
    auto h = head(21, {});
    h[0] = 'X';
    auto r = parseWbfsHead(h);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error() == FragError::WbfsInvalid);
}

TEST_CASE("wbfs_map: wbfs_sec_sz_s = 21") {
    CHECK(wbfsHeadBytes(21) == 512 + 9728);  // Kopfsektor + ALIGN_LBA(0x100 + 2 × 4482)
    auto r = parseWbfsHead(head(21, {}));
    REQUIRE(r.isOk());
    CHECK(r.value().wbfsSecSize == 2u * 1024 * 1024);
    CHECK(r.value().wlba.size() == 4482);
}

TEST_CASE("wbfs_map: Kopf zu kurz oder unsinnige Größen") {
    auto h = head(21, {});
    CHECK_FALSE(parseWbfsHead(std::span<const u8>(h).first(h.size() - 1)).isOk());
    auto badHd = head(21, {});
    badHd[8] = 12;  // 4K-Sektoren gibt es in .wbfs-Dateien nicht
    CHECK_FALSE(parseWbfsHead(badHd).isOk());
    auto badSec = head(21, {});
    badSec[9] = 14;  // kleiner als ein Wii-Sektor
    CHECK_FALSE(parseWbfsHead(badSec).isOk());
    badSec[9] = 40;
    CHECK_FALSE(parseWbfsHead(badSec).isOk());
}

TEST_CASE("wbfs_map: Blöcke 0, 1, 5 belegt") {
    auto r = parseWbfsHead(head(21, {{0, 1}, {1, 2}, {5, 7}}));
    REQUIRE(r.isOk());
    const u64 s = 2u * 1024 * 1024;
    auto runs = discRuns(r.value());
    REQUIRE(runs.size() == 2);
    CHECK(runs[0].discOffset == 0);
    CHECK(runs[0].fileOffset == 1 * s);
    CHECK(runs[0].bytes == 2 * s);
    CHECK(runs[1].discOffset == 5 * s);
    CHECK(runs[1].fileOffset == 7 * s);
    CHECK(runs[1].bytes == s);
}

TEST_CASE("wbfs_map: Nachbarblöcke mit Lücke in der Datei bleiben getrennt") {
    auto runs = discRuns(parseWbfsHead(head(21, {{0, 1}, {1, 3}})).value());
    CHECK(runs.size() == 2);
}
