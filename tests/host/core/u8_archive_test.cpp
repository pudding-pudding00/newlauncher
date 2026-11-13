#include "core/u8_archive.hpp"
#include "core/wii_title.hpp"
#include "support/u8_images.hpp"

#include <doctest/doctest.h>

using namespace nl;
using nl::test::FstSpec;

namespace {
std::vector<FstSpec> sample() {
    FstSpec meta{"meta", {}, true,
                 {FstSpec{"banner.bin", std::vector<u8>{1, 2, 3}}, FstSpec{"sound.bin", std::vector<u8>{4, 5}}}};
    FstSpec arc{"arc", {}, true, {}};
    return {meta, arc, FstSpec{"icon.bin", std::vector<u8>{9}}};
}
}  // namespace

TEST_CASE("u8: Dateien über ihren Pfad, Groß/klein egal") {
    const auto data = test::buildU8(sample());
    auto a = U8Archive::parse(data);
    REQUIRE(a.isOk());
    auto f = a.value().file("meta/banner.bin");
    REQUIRE(f.has_value());
    CHECK(f->size() == 3);
    CHECK((*f)[2] == 3);
    CHECK(a.value().file("META/Sound.bin")->size() == 2);
    CHECK(a.value().file("icon.bin")->size() == 1);
    CHECK_FALSE(a.value().file("banner.bin").has_value());  // nur unter meta/
    CHECK_FALSE(a.value().file("meta").has_value());        // Ordner ist keine Datei
}

TEST_CASE("u8: Ordner mit next auf sich selbst → InvalidData") {
    auto data = test::buildU8(sample());
    test::putBe32(data, 0x20 + 1 * 12 + 8, 1);  // Knoten 1 = meta
    CHECK(U8Archive::parse(data).error().code == ErrorCode::InvalidData);
}

TEST_CASE("u8: zu viele Knoten, falsches Magic, Daten außerhalb → InvalidData") {
    std::vector<FstSpec> many;
    for (int i = 0; i < 5000; ++i) many.push_back(FstSpec{"f", {}});
    CHECK(U8Archive::parse(test::buildU8(many)).error().code == ErrorCode::InvalidData);
    auto bad = test::buildU8(sample());
    bad[0] = 0;
    CHECK(U8Archive::parse(bad).error().code == ErrorCode::InvalidData);
    auto far = test::buildU8(sample());
    test::putBe32(far, 0x20 + 2 * 12 + 4, 0x7FFFFFF0);  // Datenoffset von banner.bin
    CHECK(U8Archive::parse(far).error().code == ErrorCode::InvalidData);
    auto name = test::buildU8(sample());
    test::putBe32(name, 0x20 + 2 * 12, 0x00FFFFF0);  // Namensoffset
    CHECK(U8Archive::parse(name).error().code == ErrorCode::InvalidData);
    CHECK(U8Archive::parse(std::span<const u8>(far.data(), 16)).error().code == ErrorCode::InvalidData);
}

TEST_CASE("u8: imetArchiveOffset findet das U8 hinter dem IMET-Kopf") {
    std::vector<u8> c(0x700, 0);
    std::memcpy(c.data() + 0x40, "IMET", 4);
    CHECK(imetArchiveOffset(c) == 0x600u);
    std::vector<u8> d(0x700, 0);
    std::memcpy(d.data() + 0x80, "IMET", 4);
    CHECK(imetArchiveOffset(d) == 0x640u);
    CHECK_FALSE(imetArchiveOffset(std::vector<u8>(0x700, 0)).has_value());
}
