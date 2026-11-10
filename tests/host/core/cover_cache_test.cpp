#include "core/cover_cache.hpp"
#include <doctest/doctest.h>
#include <cstdlib>
using namespace nl;

namespace {
struct FakeMemory : CoverMemory {
    int allocs = 0, frees = 0;
    void* alloc(u32 bytes) override { ++allocs; return std::malloc(bytes); }
    void free(void* p) override { ++frees; std::free(p); }
};
constexpr u32 kBytes = 28000;

void finish(CoverCache& c, FakeMemory& m, std::string_view stem) { c.complete(stem, m.alloc(kBytes), kBytes); }
}  // namespace

TEST_CASE("want legt Pending an, nextRequest liefert Visible vor Prefetch") {
    FakeMemory mem;
    CoverCache c(3 * kBytes, mem);
    c.beginFrame(1, 0);
    CHECK(c.want("nes/A", CoverPriority::Prefetch) == CoverState::Pending);
    CHECK(c.want("nes/B", CoverPriority::Visible) == CoverState::Pending);
    CHECK(c.want("nes/C", CoverPriority::Prefetch) == CoverState::Pending);
    CHECK_FALSE(c.idle());
    CHECK(c.nextRequest() == std::optional<std::string>("nes/B"));
    CHECK(c.nextRequest() == std::optional<std::string>("nes/C"));  // bei Gleichstand der zuletzt gewünschte
    CHECK(c.nextRequest() == std::optional<std::string>("nes/A"));
    CHECK_FALSE(c.nextRequest().has_value());
}

TEST_CASE("complete → Ready, texels/slotOf gesetzt, Einblendung über 150 ms") {
    FakeMemory mem;
    CoverCache c(3 * kBytes, mem);
    c.beginFrame(1, 1000);
    c.want("nes/A", CoverPriority::Visible);
    REQUIRE(c.nextRequest().has_value());
    CHECK_FALSE(c.slotOf("nes/A").has_value());
    finish(c, mem, "nes/A");
    CHECK(c.state("nes/A") == CoverState::Ready);
    CHECK(c.texels("nes/A") != nullptr);
    REQUIRE(c.slotOf("nes/A").has_value());
    CHECK(c.texelsOfSlot(*c.slotOf("nes/A")) == c.texels("nes/A"));
    CHECK(c.fadeAlpha("nes/A") == doctest::Approx(0.0f));
    c.beginFrame(2, 1075);
    CHECK(c.fadeAlpha("nes/A") == doctest::Approx(0.5f));
    c.beginFrame(3, 1200);
    CHECK(c.fadeAlpha("nes/A") == doctest::Approx(1.0f));
    CHECK(c.bytesUsed() == kBytes);
    CHECK(c.readyCount() == 1);
    CHECK(c.idle());
    CHECK(c.fadeAlpha("nes/X") == 0.0f);
}

TEST_CASE("Budget: ältester, nicht gewünschter Eintrag wird verdrängt") {
    FakeMemory mem;
    CoverCache c(3 * kBytes, mem);
    const char* stems[] = {"a/1", "a/2", "a/3", "a/4"};
    for (u32 i = 0; i < 4; ++i) {
        c.beginFrame(i + 1, float(i));
        c.want(stems[i], CoverPriority::Visible);
        REQUIRE(c.nextRequest().has_value());
        finish(c, mem, stems[i]);
    }
    CHECK(c.bytesUsed() <= 3 * kBytes);
    CHECK(c.state("a/1") == CoverState::Unknown);
    CHECK(c.state("a/4") == CoverState::Ready);
    CHECK(mem.frees == 1);
    CHECK(c.readyCount() == 3);
}

TEST_CASE("Gewünschte Cover werden nie verdrängt") {
    FakeMemory mem;
    CoverCache c(3 * kBytes, mem);
    const char* stems[] = {"a/1", "a/2", "a/3", "a/4"};
    c.beginFrame(1, 0);
    for (auto s : stems) c.want(s, CoverPriority::Visible);
    for (int i = 0; i < 4; ++i) {
        auto s = c.nextRequest();
        REQUIRE(s.has_value());
        finish(c, mem, *s);
    }
    CHECK(c.readyCount() == 4);
    CHECK(c.bytesUsed() == 4 * kBytes);
    c.beginFrame(2, 0);
    c.want("a/2", CoverPriority::Visible);
    CHECK_FALSE(c.nextRequest().has_value());
    CHECK(c.bytesUsed() <= 3 * kBytes);
    CHECK(c.state("a/2") == CoverState::Ready);
}

TEST_CASE("Veraltete Pending-Anfragen verfallen") {
    FakeMemory mem;
    CoverCache c(3 * kBytes, mem);
    c.beginFrame(1, 0);
    c.want("a/1", CoverPriority::Visible);
    c.beginFrame(3, 0);
    CHECK_FALSE(c.nextRequest().has_value());
    CHECK(c.state("a/1") == CoverState::Unknown);
    CHECK(c.idle());
    c.beginFrame(4, 0);
    CHECK(c.want("a/1", CoverPriority::Visible) == CoverState::Pending);
    CHECK(c.nextRequest() == std::optional<std::string>("a/1"));
}

TEST_CASE("fail → Missing, invalidate → Unknown → wieder Pending") {
    FakeMemory mem;
    CoverCache c(3 * kBytes, mem);
    c.beginFrame(1, 0);
    c.want("wii/RMCP01", CoverPriority::Visible);
    REQUIRE(c.nextRequest().has_value());
    c.fail("wii/RMCP01");
    CHECK(c.state("wii/RMCP01") == CoverState::Missing);
    c.beginFrame(2, 0);
    CHECK(c.want("wii/RMCP01", CoverPriority::Visible) == CoverState::Missing);
    CHECK_FALSE(c.nextRequest().has_value());
    c.invalidate("wii/RMCP01");
    CHECK(c.state("wii/RMCP01") == CoverState::Unknown);
    c.beginFrame(3, 0);
    CHECK(c.want("wii/RMCP01", CoverPriority::Visible) == CoverState::Pending);
    CHECK(c.nextRequest().has_value());
}

TEST_CASE("In Arbeit wird nicht doppelt geliefert, complete nach Verfall wird übernommen") {
    FakeMemory mem;
    CoverCache c(3 * kBytes, mem);
    c.beginFrame(1, 0);
    c.want("a/1", CoverPriority::Visible);
    REQUIRE(c.nextRequest().has_value());
    c.want("a/1", CoverPriority::Visible);
    CHECK_FALSE(c.nextRequest().has_value());
    CHECK_FALSE(c.idle());
    c.beginFrame(9, 0);
    finish(c, mem, "a/1");
    CHECK(c.state("a/1") == CoverState::Ready);
    finish(c, mem, "a/unbekannt");  // nicht angefragt (z. B. nach clear): sofort freigegeben
    CHECK(mem.frees == 1);
}

TEST_CASE("clear gibt alles frei, Generation zählt Änderungen") {
    FakeMemory mem;
    CoverCache c(3 * kBytes, mem);
    const u32 g0 = c.generation();
    c.beginFrame(1, 0);
    c.want("a/1", CoverPriority::Visible);
    c.want("a/2", CoverPriority::Visible);
    while (auto s = c.nextRequest()) finish(c, mem, *s);
    CHECK(c.generation() != g0);
    c.clear();
    CHECK(mem.allocs == mem.frees);
    CHECK(c.bytesUsed() == 0);
    CHECK(c.state("a/1") == CoverState::Unknown);
}

TEST_CASE("requestedAtMs ab dem ersten Visible-want") {
    FakeMemory m;
    CoverCache c(3 * kBytes, m);
    c.beginFrame(1, 100);
    c.want("nes/A", CoverPriority::Prefetch);
    CHECK_FALSE(c.requestedAtMs("nes/A").has_value());
    c.beginFrame(2, 116);
    c.want("nes/A", CoverPriority::Visible);
    REQUIRE(c.requestedAtMs("nes/A").has_value());
    CHECK(*c.requestedAtMs("nes/A") == doctest::Approx(116.0f));
    c.beginFrame(3, 132);
    c.want("nes/A", CoverPriority::Visible);
    CHECK(*c.requestedAtMs("nes/A") == doctest::Approx(116.0f));
    finish(c, m, "nes/A");
    CHECK_FALSE(c.requestedAtMs("nes/A").has_value());
    CHECK_FALSE(c.requestedAtMs("nes/unbekannt").has_value());
}
