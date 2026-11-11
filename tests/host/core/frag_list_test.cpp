#include "core/frag_list.hpp"
#include <doctest/doctest.h>
using namespace nl;

namespace {
constexpr u64 KiB = 1024, MiB = 1024 * KiB, GiB = 1024 * MiB;

bool same(const FragEntry& a, const FragEntry& b) {
    return a.offset == b.offset && a.sector == b.sector && a.count == b.count;
}
}  // namespace

TEST_CASE("frag_list: ISO mit zwei Extents") {
    FilePart iso{1000 * 512 + 1, {{0, 5000, 600}, {600, 9000, 408}}};  // Extents clustergerade, also länger
    auto r = buildIsoFragList(iso);
    REQUIRE(r.isOk());
    CHECK(r.value().size == 1001);
    REQUIRE(r.value().entries.size() == 2);
    CHECK(same(r.value().entries[0], {0, 5000, 600}));
    CHECK(same(r.value().entries[1], {600, 9000, 401}));  // auf die Dateigröße gekürzt
}

TEST_CASE("frag_list: ISO länger als ihre Extents") {
    FilePart iso{1000 * 512, {{0, 5000, 600}}};
    auto r = buildIsoFragList(iso);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error() == FragError::OutOfFile);
}

TEST_CASE("frag_list: WBFS-Block über zwei Extents") {
    WbfsLayout w{u32(2 * MiB), {1}};  // Disc-Block 0 liegt ab Dateioffset 2 MiB
    FilePart f{4 * MiB, {{0, 100, 5000}, {5000, 20000, 3192}}};
    auto r = buildWbfsFragList(w, std::span<const FilePart>(&f, 1));
    REQUIRE(r.isOk());
    REQUIRE(r.value().entries.size() == 2);
    // Dateisektor 4096 … 8191: 904 Sektoren im ersten Extent, 3192 im zweiten
    CHECK(same(r.value().entries[0], {0, 100 + 4096, 904}));
    CHECK(same(r.value().entries[1], {904, 20000, 3192}));
    CHECK(r.value().size == 4096);
}

TEST_CASE("frag_list: Split-Grenze") {
    const u64 split = 4 * GiB - 32 * KiB;
    WbfsLayout w{u32(MiB), std::vector<u16>{4095, 4096}};  // 2 MiB ab Dateioffset 4 GiB − 1 MiB
    std::vector<FilePart> parts{{split, {{0, 100000, u32(split / 512)}}}, {8 * MiB, {{0, 50000000, 16384}}}};
    auto r = buildWbfsFragList(w, parts);
    REQUIRE(r.isOk());
    REQUIRE(r.value().entries.size() == 2);
    const u32 first = u32((MiB - 32 * KiB) / 512);
    CHECK(same(r.value().entries[0], {0, u32(100000 + (4 * GiB - MiB) / 512), first}));
    CHECK(same(r.value().entries[1], {first, 50000000, u32(2 * MiB / 512) - first}));
    CHECK(r.value().size == 4096);
}

TEST_CASE("frag_list: Teil fehlt") {
    const u64 split = 4 * GiB - 32 * KiB;
    WbfsLayout w{u32(MiB), std::vector<u16>{9000}};  // ≈ 8,8 GiB: jenseits des zweiten Teils
    std::vector<FilePart> parts{{split, {{0, 100000, u32(split / 512)}}}, {8 * MiB, {{0, 50000000, 16384}}}};
    auto r = buildWbfsFragList(w, parts);
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error() == FragError::PartMissing);
}

TEST_CASE("frag_list: Block hinter dem Dateiende") {
    WbfsLayout w{u32(2 * MiB), {3}};
    FilePart f{4 * MiB, {{0, 100, 8192}}};
    auto r = buildWbfsFragList(w, std::span<const FilePart>(&f, 1));
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error() == FragError::OutOfFile);
}

TEST_CASE("frag_list: verschmelzen") {
    WbfsLayout w{u32(2 * MiB), {1, 0, 2}};  // Disc-Block 1 leer: Offset springt, LBA nicht
    FilePart f{6 * MiB, {{0, 100, 12288}}};
    auto r = buildWbfsFragList(w, std::span<const FilePart>(&f, 1));
    REQUIRE(r.isOk());
    REQUIRE(r.value().entries.size() == 2);  // Lücke im Offset, also nicht verschmolzen

    // Dateiblöcke 1 und 3 liegen nicht nebeneinander in der Datei, aber zufällig auf dem Gerät:
    // Offset und LBA lückenlos → ein Eintrag.
    WbfsLayout w2{u32(2 * MiB), {1, 3}};
    FilePart f2{8 * MiB, {{0, 100, 8192}, {8192, 50000, 4096}, {12288, 8292, 4096}}};
    auto r2 = buildWbfsFragList(w2, std::span<const FilePart>(&f2, 1));
    REQUIRE(r2.isOk());
    REQUIRE(r2.value().entries.size() == 1);
    CHECK(same(r2.value().entries[0], {0, 100 + 4096, 8192}));
}

TEST_CASE("frag_list: 20001 Einträge") {
    std::vector<Extent> ex;
    for (u32 i = 0; i < 20001; ++i) ex.push_back({i, 10 + 2 * i, 1});  // jede LBA-Lücke verhindert Verschmelzen
    auto r = buildIsoFragList(FilePart{20001 * 512, ex});
    REQUIRE_FALSE(r.isOk());
    CHECK(r.error() == FragError::TooFragmented);

    ex.pop_back();
    CHECK(buildIsoFragList(FilePart{20000 * 512, ex}).isOk());
}

TEST_CASE("frag_list: serialize golden") {
    auto b = serializeFragList(FragList{100, {{0, 2048, 64}, {64, 9000, 36}}});
    REQUIRE(b.size() == 240012);
    const u8 want[36] = {0, 0, 0, 100, 0, 0, 0, 2, 0, 0, 0x4E, 0x20,  //
                         0, 0, 0, 0,   0, 0, 8, 0, 0, 0, 0,    64,    //
                         0, 0, 0, 64,  0, 0, 0x23, 0x28, 0, 0, 0, 36};
    for (int i = 0; i < 36; ++i) {
        CAPTURE(i);
        CHECK(b[i] == want[i]);
    }
    bool restZero = true;
    for (std::size_t i = 36; i < b.size(); ++i) restZero = restZero && b[i] == 0;
    CHECK(restZero);
}

TEST_CASE("frag_list: crc") {
    FragList l{100, {{0, 2048, 64}, {64, 9000, 36}}};
    const u32 a = fragListCrc(serializeFragList(l));
    CHECK(a == fragListCrc(serializeFragList(l)));
    l.entries[1].count = 37;
    CHECK(a != fragListCrc(serializeFragList(l)));
}
