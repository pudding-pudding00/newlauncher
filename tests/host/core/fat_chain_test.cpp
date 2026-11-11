#include "core/fat_chain.hpp"
#include "support/fat_image.hpp"
#include <doctest/doctest.h>
using namespace nl;
using nl::test::Fat32Builder;
using nl::test::SparseDisk;

namespace {
constexpr u32 kSpc = 8;              // 4 KiB-Cluster
constexpr u32 kTotal = 600000;       // ≈ 74 900 Cluster → FAT32
constexpr u64 kCluster = kSpc * 512;
}  // namespace

TEST_CASE("fat_chain: Volume hinter MBR") {
    SparseDisk d;
    Fat32Builder b(d, 2048, kSpc, kTotal);
    auto v = findFatVolume(d);
    REQUIRE(v.isOk());
    CHECK(v.value().partLba == 2048);
    CHECK(v.value().fatLba == b.fatLba);
    CHECK(v.value().dataLba == b.dataLba);
    CHECK(v.value().sectorsPerCluster == kSpc);
    CHECK(v.value().clusterCount == b.clusterCount);
    CHECK(b.clusterCount >= 70000);
}

TEST_CASE("fat_chain: Karte ohne MBR") {
    SparseDisk d;
    Fat32Builder b(d, 0, kSpc, kTotal, /*withMbr=*/false);
    auto v = findFatVolume(d);
    REQUIRE(v.isOk());
    CHECK(v.value().partLba == 0);
    CHECK(v.value().dataLba == b.dataLba);
}

TEST_CASE("fat_chain: zweite Partition") {
    SparseDisk d;
    nl::test::writeNtfsBoot(d, 2048);
    nl::test::writeMbrEntry(d, 0, 0x07, 2048, 100000);
    Fat32Builder b(d, 200000, kSpc, kTotal, /*withMbr=*/false);
    nl::test::writeMbrEntry(d, 1, 0x0C, 200000, kTotal);
    auto v = findFatVolume(d);
    REQUIRE(v.isOk());
    CHECK(v.value().partLba == 200000);
}

TEST_CASE("fat_chain: GPT-Schutz-MBR") {
    SparseDisk d;
    nl::test::writeMbrEntry(d, 0, 0xEE, 1, 0xFFFFFFFFu);
    d.sector(1)[0] = 'E';  // „EFI PART“-Kopf, kein Bootsektor
    auto v = findFatVolume(d);
    REQUIRE_FALSE(v.isOk());
    CHECK(v.error() == FatError::NoFatVolume);
}

TEST_CASE("fat_chain: leere Platte") {
    SparseDisk d;
    auto v = findFatVolume(d);
    REQUIRE_FALSE(v.isOk());
    CHECK(v.error() == FatError::NoFatVolume);
}

TEST_CASE("fat_chain: FAT16-SD") {
    SparseDisk d;
    Fat32Builder b(d, 8192, 4, 120500, true, /*fat16=*/true);
    CHECK(b.clusterCount > 4085);
    CHECK(b.clusterCount < 65525);
    auto v = findFatVolume(d);
    REQUIRE_FALSE(v.isOk());
    CHECK(v.error() == FatError::NotFat32);
}

TEST_CASE("fat_chain: 4K-Sektoren") {
    SparseDisk d(4096);
    auto v = findFatVolume(d);
    REQUIRE_FALSE(v.isOk());
    CHECK(v.error() == FatError::SectorSize);
}

namespace {
struct Vol {
    SparseDisk d;
    Fat32Builder b{d, 2048, kSpc, kTotal};
    FatVolume v = findFatVolume(d).value();
};
}  // namespace

TEST_CASE("fat_chain: zusammenhängende Datei ergibt ein Extent") {
    Vol t;
    t.b.setChain({10, 11, 12, 13, 14, 15, 16, 17, 18, 19});
    auto e = fileExtents(t.d, t.v, 10, 10 * kCluster);
    REQUIRE(e.isOk());
    REQUIRE(e.value().size() == 1);
    CHECK(e.value()[0] == Extent{0, t.b.clusterLba(10), 80});
}

TEST_CASE("fat_chain: fragmentierte Datei") {
    Vol t;
    t.b.setChain({10, 11, 50, 51, 52, 12});
    auto e = fileExtents(t.d, t.v, 10, 6 * kCluster);
    REQUIRE(e.isOk());
    REQUIRE(e.value().size() == 3);
    CHECK(e.value()[0] == Extent{0, t.b.clusterLba(10), 16});
    CHECK(e.value()[1] == Extent{16, t.b.clusterLba(50), 24});
    CHECK(e.value()[2] == Extent{40, t.b.clusterLba(12), 8});
}

TEST_CASE("fat_chain: Dateigröße nicht clustergerade") {
    Vol t;
    t.b.setChain({20, 21, 22});
    auto e = fileExtents(t.d, t.v, 20, 2 * kCluster + 1);
    REQUIRE(e.isOk());
    REQUIRE(e.value().size() == 1);
    CHECK(e.value()[0].count == 24);  // letzter Cluster zählt voll
}

TEST_CASE("fat_chain: Kette zu kurz") {
    Vol t;
    t.b.setChain({30, 31, 32});
    auto e = fileExtents(t.d, t.v, 30, 5 * kCluster);
    REQUIRE_FALSE(e.isOk());
    CHECK(e.error() == FatError::ChainTooShort);
}

TEST_CASE("fat_chain: Kette länger als die Datei") {
    Vol t;
    t.b.setChain({30, 31, 32, 33});
    auto e = fileExtents(t.d, t.v, 30, 3 * kCluster);
    REQUIRE_FALSE(e.isOk());
    CHECK(e.error() == FatError::BadChain);
}

TEST_CASE("fat_chain: Schleife endet") {
    for (u64 size : {u64{1} << 30, 1000 * kCluster}) {
        CAPTURE(size);
        Vol t;
        t.b.setEntry(10, 11);
        t.b.setEntry(11, 10);
        const u32 before = t.d.reads;
        auto e = fileExtents(t.d, t.v, 10, size);
        REQUIRE_FALSE(e.isOk());
        CHECK(e.error() == FatError::BadChain);
        CHECK(t.d.reads - before <= t.v.clusterCount + 1);
    }
}

TEST_CASE("fat_chain: freier oder defekter Cluster in der Kette") {
    for (u32 bad : {0u, 1u, 0x0FFFFFF7u}) {
        CAPTURE(bad);
        Vol t;
        t.b.setEntry(40, 41);
        t.b.setEntry(41, bad);
        auto e = fileExtents(t.d, t.v, 40, 4 * kCluster);
        REQUIRE_FALSE(e.isOk());
        CHECK(e.error() == FatError::BadChain);
    }
}

TEST_CASE("fat_chain: Cluster außerhalb des Volumes und obere Bits") {
    Vol t;
    t.b.setEntry(40, t.v.clusterCount + 2);  // erster ungültiger Cluster
    auto e = fileExtents(t.d, t.v, 40, 2 * kCluster);
    REQUIRE_FALSE(e.isOk());
    CHECK(e.error() == FatError::BadChain);
    CHECK_FALSE(fileExtents(t.d, t.v, 1, kCluster).isOk());  // Startcluster < 2

    t.b.setEntry(60, 0xF0000000u | 61);  // die oberen 4 Bit sind reserviert
    t.b.setEntry(61, 0xFFFFFFFFu);
    auto ok = fileExtents(t.d, t.v, 60, 2 * kCluster);
    REQUIRE(ok.isOk());
    CHECK(ok.value()[0].count == 16);
}

TEST_CASE("fat_chain: leere Datei") {
    Vol t;
    auto e = fileExtents(t.d, t.v, 0, 0);
    REQUIRE(e.isOk());
    CHECK(e.value().empty());
}
