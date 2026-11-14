#include "core/gc_fst.hpp"
#include "support/gc_disc_images.hpp"

#include <doctest/doctest.h>

using namespace nl;
using nl::test::FstSpec;

namespace {
std::vector<FstSpec> sampleRoot() {
    FstSpec sub{"audio", {}, true, {FstSpec{"inner.bnr", std::vector<u8>(8, 1)}}};
    return {FstSpec{"boot.dat", std::vector<u8>(16, 2)}, sub, FstSpec{"opening.bnr", std::vector<u8>(40, 3)}};
}
}  // namespace

TEST_CASE("gc_fst: findRootFile findet Dateien der Wurzel ohne Rücksicht auf Groß/klein") {
    const auto fst = test::buildFst(sampleRoot(), 0x1000);
    auto r = findRootFile(fst, "OPENING.BNR");
    REQUIRE(r.isOk());
    CHECK(r.value().size == 40);
    CHECK(r.value().offset == 0x1000 + 32 + 32);  // boot.dat (16 → 32), inner.bnr (8 → 32)
}

TEST_CASE("gc_fst: Dateien in Unterordnern zählen nicht") {
    const auto fst = test::buildFst(sampleRoot(), 0x1000);
    auto r = findRootFile(fst, "inner.bnr");
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().code == ErrorCode::NotFound);
}

TEST_CASE("gc_fst: Namensoffset außerhalb → InvalidData") {
    auto fst = test::buildFst(sampleRoot(), 0x1000);
    test::putBe32(fst, 12, 0x00FFFFF0);  // erster Eintrag nach der Wurzel: Namensoffset weit hinter dem Ende
    auto r = findRootFile(fst, "opening.bnr");
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error().code == ErrorCode::InvalidData);
}

TEST_CASE("gc_fst: kaputte Wurzel und rückwärts zeigendes next → InvalidData, keine Schleife") {
    auto fst = test::buildFst(sampleRoot(), 0x1000);
    auto broken = fst;
    test::putBe32(broken, 8, 100000);  // mehr Einträge als Bytes
    CHECK(findRootFile(broken, "opening.bnr").error().code == ErrorCode::InvalidData);
    auto loop = fst;
    test::putBe32(loop, 2 * 12 + 8, 1);  // Ordner „audio“ zeigt mit next zurück
    CHECK(findRootFile(loop, "opening.bnr").error().code == ErrorCode::InvalidData);
    CHECK(findRootFile(std::span<const u8>(fst.data(), 8), "x").error().code == ErrorCode::InvalidData);
}
