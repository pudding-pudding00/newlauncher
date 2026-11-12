#include "core/cios_scan.hpp"

#include "core/ini.hpp"

#include <array>

namespace nl {
namespace {

const CiosEntry* entryAt(std::span<const CiosEntry> found, u8 slot) {
    for (const CiosEntry& e : found)
        if (e.slot == slot) return &e;
    return nullptr;
}

bool isD2xAt(std::span<const CiosEntry> found, u8 slot) {
    const CiosEntry* e = entryAt(found, slot);
    return e != nullptr && e->kind == CiosKind::D2x;
}

}  // namespace

CiosKind classifySlot(u16 titleVersion, u32 contentCount, const std::optional<IosInfo>& info) {
    if (titleVersion == kStubTitleVersion || contentCount <= 1) return CiosKind::Stub;
    return info ? CiosKind::D2x : CiosKind::Other;
}

const char* ciosKindName(CiosKind k) {
    switch (k) {
        case CiosKind::D2x: return "d2x";
        case CiosKind::Stub: return "stub";
        case CiosKind::Other: return "other";
    }
    return "other";
}

CiosChoice chooseCios(std::span<const CiosEntry> found, std::optional<u8> gameIos, std::optional<u8> configIos) {
    if (gameIos) return {gameIos, true};
    if (configIos) return {configIos, true};
    constexpr std::array<u8, 3> kPreferred{249, 250, 248};
    for (u8 slot : kPreferred)
        if (isD2xAt(found, slot)) return {slot, false};
    std::optional<u8> highest;
    for (const CiosEntry& e : found)
        if (e.kind == CiosKind::D2x && (!highest || e.slot > *highest)) highest = e.slot;
    if (highest) return {highest, false};
    if (const CiosEntry* e = entryAt(found, kDefaultCiosSlot); e != nullptr && e->kind == CiosKind::Other) {
        return {kDefaultCiosSlot, false};
    }
    return {};
}

std::optional<u8> parseIosValue(std::string_view v) {
    v = trim(v);
    if (v.empty() || v.size() > 3) return std::nullopt;
    unsigned n = 0;
    for (char ch : v) {
        if (ch < '0' || ch > '9') return std::nullopt;
        n = n * 10 + static_cast<unsigned>(ch - '0');
    }
    if (n < kCiosFirstSlot || n > kCiosLastSlot) return std::nullopt;
    return static_cast<u8>(n);
}

bool isBadIosValue(std::string_view v) {
    v = trim(v);
    return !v.empty() && v != "auto" && !parseIosValue(v);
}

CiosStatus ciosStatusFor(std::span<const CiosEntry> found, const CiosChoice& choice) {
    CiosStatus s;
    if (!choice.slot) return s;
    s.slot = *choice.slot;
    const CiosEntry* e = entryAt(found, *choice.slot);
    if (e == nullptr || e->kind == CiosKind::Stub) return s;
    if (e->kind == CiosKind::D2x) {
        s.state = CiosState::D2x;
        s.base = e->base;
        s.version = e->version;
    } else {
        s.state = CiosState::Unverified;
    }
    return s;
}

}  // namespace nl
