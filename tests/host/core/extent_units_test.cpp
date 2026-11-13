#include "core/extent_units.hpp"
#include <doctest/doctest.h>
using namespace nl;

namespace {
constexpr u32 kCluster = 4096;  // 8 Sektoren
constexpr u64 kPart = 133120;   // Partitionsbeginn in Gerätesektoren
}  // namespace

TEST_CASE("extent_units: 4-KiB-Cluster, zwei Läufe → 512-Byte-Sektoren ab Partitions-LBA") {
    const FsRun runs[] = {{0, 100, 4, false}, {4, 300, 2, false}};
    auto r = runsToExtents(runs, kCluster, kPart, 512, 6 * kCluster);
    REQUIRE(r.isOk());
    REQUIRE(r.value().size() == 2);
    CHECK(r.value()[0] == Extent{0, static_cast<u32>(kPart + 800), 32});
    CHECK(r.value()[1] == Extent{32, static_cast<u32>(kPart + 2400), 16});
}

TEST_CASE("extent_units: 4-KiB-Gerätesektoren – partLba wird ×8 umgerechnet") {
    const FsRun runs[] = {{0, 10, 1, false}};
    auto r = runsToExtents(runs, kCluster, 1000, 4096, kCluster);
    REQUIRE(r.isOk());
    REQUIRE(r.value().size() == 1);
    CHECK(r.value()[0] == Extent{0, 8000 + 80, 8});
}

TEST_CASE("extent_units: Lauf mit Loch → Unsupported") {
    const FsRun runs[] = {{0, 100, 1, false}, {1, 0, 1, true}, {2, 102, 1, false}};
    auto r = runsToExtents(runs, kCluster, kPart, 512, 3 * kCluster);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error() == ExtentError::Unsupported);
}

TEST_CASE("extent_units: Lücke zwischen Läufen → Gap") {
    const FsRun runs[] = {{0, 100, 1, false}, {2, 102, 1, false}};
    auto r = runsToExtents(runs, kCluster, kPart, 512, 3 * kCluster);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error() == ExtentError::Gap);
}

TEST_CASE("extent_units: Läufe decken die Datei nicht ganz ab → Gap") {
    const FsRun runs[] = {{0, 100, 1, false}};
    auto r = runsToExtents(runs, kCluster, kPart, 512, 2 * kCluster);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error() == ExtentError::Gap);
}

TEST_CASE("extent_units: letzte Einheit teilweise belegt → Ende bei ceil(fileSize/512)") {
    const FsRun runs[] = {{0, 100, 2, false}};
    auto r = runsToExtents(runs, kCluster, 0, 512, kCluster + 1000);
    REQUIRE(r.isOk());
    REQUIRE(r.value().size() == 1);
    CHECK(r.value()[0] == Extent{0, 800, 8 + 2});
}

TEST_CASE("extent_units: Läufe hinter dem Dateiende (auch Löcher) zählen nicht") {
    const FsRun runs[] = {{0, 100, 1, false}, {1, 0, 5, true}};
    auto r = runsToExtents(runs, kCluster, 0, 512, kCluster);
    REQUIRE(r.isOk());
    CHECK(r.value().size() == 1);
}

TEST_CASE("extent_units: LBA ab 2^32 Sektoren → Overflow") {
    const FsRun runs[] = {{0, (u64{1} << 32) / 8, 1, false}};
    auto r = runsToExtents(runs, kCluster, 0, 512, kCluster);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error() == ExtentError::Overflow);
    const FsRun last[] = {{0, (u64{1} << 32) / 8 - 1, 1, false}};
    CHECK(runsToExtents(last, kCluster, 0, 512, kCluster).isOk());  // endet genau bei 2^32
}

TEST_CASE("extent_units: angrenzende Läufe → ein Extent") {
    const FsRun runs[] = {{0, 100, 2, false}, {2, 102, 3, false}};
    auto r = runsToExtents(runs, kCluster, 2048, 512, 5 * kCluster);
    REQUIRE(r.isOk());
    REQUIRE(r.value().size() == 1);
    CHECK(r.value()[0] == Extent{0, 2048 + 800, 40});
}

TEST_CASE("extent_units: leere Datei → keine Extents; Einheit kein Vielfaches von 512 → Unsupported") {
    auto empty = runsToExtents({}, kCluster, 0, 512, 0);
    REQUIRE(empty.isOk());
    CHECK(empty.value().empty());
    const FsRun runs[] = {{0, 1, 1, false}};
    auto odd = runsToExtents(runs, 1000, 0, 512, 1000);
    REQUIRE_FALSE(odd.isOk());
    CHECK(odd.error() == ExtentError::Unsupported);
    auto badSector = runsToExtents(runs, kCluster, 0, 1000, kCluster);
    REQUIRE_FALSE(badSector.isOk());
    CHECK(badSector.error() == ExtentError::Unsupported);
}
