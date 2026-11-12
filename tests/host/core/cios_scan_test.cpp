#include "core/cios_scan.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace nl;

namespace {
CiosEntry d2x(u8 slot, u32 base = 56) { return CiosEntry{slot, CiosKind::D2x, base, 21, "d2x", "v10"}; }
CiosEntry stub(u8 slot) { return CiosEntry{slot, CiosKind::Stub, kDefaultCiosBase, 0xFF00, "", ""}; }
CiosEntry other(u8 slot) { return CiosEntry{slot, CiosKind::Other, kDefaultCiosBase, 3, "", ""}; }
CiosChoice choose(const std::vector<CiosEntry>& found, std::optional<u8> game = {}, std::optional<u8> cfg = {}) {
    return chooseCios(found, game, cfg);
}
IosInfo d2xInfo() { return IosInfo{10, 57, "d2x", "v10"}; }
}  // namespace

TEST_CASE("cios_scan: classifySlot") {
    CHECK(classifySlot(0xFF00, 3, d2xInfo()) == CiosKind::Stub);  // Stub schlägt sogar eine lesbare Info
    CHECK(classifySlot(21, 1, std::nullopt) == CiosKind::Stub);
    CHECK(classifySlot(21, 3, d2xInfo()) == CiosKind::D2x);
    CHECK(classifySlot(21, 3, std::nullopt) == CiosKind::Other);  // Hermes, Waninkoko: ohne ios_info_t
}

TEST_CASE("cios_scan: Stub in 249, d2x in 250 → 250") {
    const CiosChoice c = choose({stub(249), d2x(250)});
    CHECK(c.slot == std::optional<u8>(250));
    CHECK_FALSE(c.forced);
}

TEST_CASE("cios_scan: d2x in 248 und 251 → 248 (Vorzugsliste 249, 250, 248)") {
    CHECK(choose({d2x(251), d2x(248)}).slot == std::optional<u8>(248));
}

TEST_CASE("cios_scan: nur d2x in 222 und 224 → höchster Slot") {
    CHECK(choose({d2x(222), d2x(224)}).slot == std::optional<u8>(224));
}

TEST_CASE("cios_scan: nur Other in 249 → 249, nur Other in 250 → nichts") {
    CHECK(choose({other(249)}).slot == std::optional<u8>(249));
    CHECK_FALSE(choose({other(250)}).slot.has_value());
    CHECK_FALSE(choose({}).slot.has_value());
    CHECK_FALSE(choose({stub(249)}).slot.has_value());
}

TEST_CASE("cios_scan: gameIos gewinnt und ist festgelegt, auch wenn der Slot fehlt") {
    const CiosChoice there = choose({d2x(249), d2x(222)}, u8{222});
    CHECK(there.slot == std::optional<u8>(222));
    CHECK(there.forced);
    const CiosChoice missing = choose({d2x(249)}, u8{222});
    CHECK(missing.slot == std::optional<u8>(222));  // kein Rückfall auf 249
    CHECK(missing.forced);
}

TEST_CASE("cios_scan: configIos schlägt die Automatik, verliert gegen gameIos") {
    const CiosChoice cfg = choose({d2x(249), d2x(250)}, std::nullopt, u8{250});
    CHECK(cfg.slot == std::optional<u8>(250));
    CHECK(cfg.forced);
    CHECK(choose({d2x(249), d2x(250)}, u8{249}, u8{250}).slot == std::optional<u8>(249));
}

TEST_CASE("cios_scan: parseIosValue") {
    CHECK_FALSE(parseIosValue("auto").has_value());
    CHECK_FALSE(parseIosValue("").has_value());
    CHECK(parseIosValue("200") == std::optional<u8>(200));
    CHECK(parseIosValue(" 249 ") == std::optional<u8>(249));
    CHECK(parseIosValue("255") == std::optional<u8>(255));
    for (const char* bad : {"199", "256", "abc", "24x", "-249", "0249x"}) {
        CAPTURE(bad);
        CHECK_FALSE(parseIosValue(bad).has_value());
    }
    CHECK_FALSE(isBadIosValue("auto"));
    CHECK_FALSE(isBadIosValue(""));
    CHECK_FALSE(isBadIosValue("250"));
    CHECK(isBadIosValue("199"));
    CHECK(isBadIosValue("abc"));
}

TEST_CASE("cios_scan: ciosStatusFor") {
    const std::vector<CiosEntry> found{stub(249), d2x(250, 57), other(251)};
    const CiosStatus a = ciosStatusFor(found, CiosChoice{u8{250}, false});
    CHECK(a.state == CiosState::D2x);
    CHECK(a.slot == 250);
    CHECK(a.base == 57);
    CHECK(a.version == "v10");
    const CiosStatus o = ciosStatusFor(found, CiosChoice{u8{251}, true});
    CHECK(o.state == CiosState::Unverified);
    CHECK(o.slot == 251);
    CHECK(o.base == kDefaultCiosBase);
    const CiosStatus s = ciosStatusFor(found, CiosChoice{u8{249}, true});  // festgelegter Stub
    CHECK(s.state == CiosState::Missing);
    CHECK(s.slot == 249);
    const CiosStatus gone = ciosStatusFor(found, CiosChoice{u8{222}, true});
    CHECK(gone.state == CiosState::Missing);
    CHECK(gone.slot == 222);
    const CiosStatus none = ciosStatusFor(found, CiosChoice{});
    CHECK(none.state == CiosState::Missing);
    CHECK(none.slot == kDefaultCiosSlot);
}

TEST_CASE("cios_scan: ciosKindName für Log und Systeminfo") {
    CHECK(std::string(ciosKindName(CiosKind::D2x)) == "d2x");
    CHECK(std::string(ciosKindName(CiosKind::Stub)) == "stub");
    CHECK(std::string(ciosKindName(CiosKind::Other)) == "other");
}
