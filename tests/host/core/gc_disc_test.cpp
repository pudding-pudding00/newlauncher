#include "core/gc_disc.hpp"
#include "support/gc_disc_images.hpp"
#include "support/mem_fs.hpp"

#include <doctest/doctest.h>

using namespace nl;
using nl::test::BnrText;
using nl::test::FstSpec;

namespace {
std::vector<u8> sampleBnr() { return test::gcBnr(false, {BnrText{"S", "M", "Metroid", "Nintendo", "Text"}}); }
std::vector<u8> sampleIso() {
    return test::gcIsoWithFiles("GM8P01", "Metroid Prime",
                                {FstSpec{"a.dat", std::vector<u8>(0x9000, 7)}, FstSpec{"opening.bnr", sampleBnr()}});
}
}  // namespace

TEST_CASE("gc_disc: opening.bnr aus einem ISO") {
    test::MemFileSystem fs;
    fs.addFile("sd:/games/M/game.iso", sampleIso());
    auto r = loadGcBanner(fs, "sd:/games/M/game.iso", 0, false);
    REQUIRE(r.isOk());
    CHECK(r.value().title == "Metroid");
}

TEST_CASE("gc_disc: opening.bnr aus einem CISO mit Lücken in der Blockkarte") {
    test::MemFileSystem fs;
    auto iso = sampleIso();
    iso.resize(0x30000, 0);  // leere Blöcke dazwischen und am Ende
    const auto ciso = test::cisoFromIso(iso);
    fs.addFile("sd:/games/M/game.ciso", ciso);
    auto r = loadGcBanner(fs, "sd:/games/M/game.ciso", 0, false);
    REQUIRE(r.isOk());
    CHECK(r.value().maker == "Nintendo");
}

TEST_CASE("gc_disc: entpacktes Abbild sucht root/ (Nintendont) und files/ (Dolphin)") {
    test::MemFileSystem fs;
    fs.addFile("sd:/games/A/sys/boot.bin", test::gcIso("GM8P01", "x", 0x440));
    fs.addFile("sd:/games/A/root/Opening.bnr", sampleBnr());
    fs.addFile("sd:/games/B/sys/boot.bin", test::gcIso("GM8P01", "x", 0x440));
    fs.addFile("sd:/games/B/files/opening.bnr", sampleBnr());
    CHECK(loadGcBanner(fs, "sd:/games/A", 0, false).isOk());
    CHECK(loadGcBanner(fs, "sd:/games/B", 0, false).isOk());
}

TEST_CASE("gc_disc: ohne opening.bnr → NotFound, kaputter FST-Kopf → InvalidData") {
    test::MemFileSystem fs;
    fs.addFile("sd:/a.iso", test::gcIsoWithFiles("GM8P01", "x", {FstSpec{"a.dat", std::vector<u8>(4, 1)}}));
    CHECK(loadGcBanner(fs, "sd:/a.iso", 0, false).error().code == ErrorCode::NotFound);
    auto bad = sampleIso();
    test::putBe32(bad, 0x428, 0x7FFFFFFF);  // FST-Größe absurd
    fs.addFile("sd:/b.iso", bad);
    CHECK(loadGcBanner(fs, "sd:/b.iso", 0, false).error().code == ErrorCode::InvalidData);
    fs.addFile("sd:/c/sys/boot.bin", test::gcIso("GM8P01", "x", 0x440));
    CHECK(loadGcBanner(fs, "sd:/c", 0, false).error().code == ErrorCode::NotFound);
    CHECK_FALSE(loadGcBanner(fs, "sd:/fehlt.iso", 0, false).isOk());
}
