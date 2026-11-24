#include "core/disc_id.hpp"

#include "core/disc_header.hpp"
#include "core/ini.hpp"

namespace nl {

namespace {

constexpr u32 kWiiMagic = 0x5D1C9EA3u;
constexpr u32 kGcMagic = 0xC2339F3Du;

u32 be32(std::span<const u8> b, std::size_t at) {
    return (static_cast<u32>(b[at]) << 24) | (static_cast<u32>(b[at + 1]) << 16) |
           (static_cast<u32>(b[at + 2]) << 8) | b[at + 3];
}

bool validId6Chars(std::span<const u8> b) {
    if (b.size() < 6) return false;
    for (std::size_t i = 0; i < 6; ++i) {
        const u8 c = b[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    }
    return true;
}

char upperAscii(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }

bool ieqPrefix(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (upperAscii(s[i]) != upperAscii(prefix[i])) return false;
    return true;
}

}  // namespace

OpticalDisc classifyDisc(std::span<const u8> header) {
    if (header.empty()) return {DiscKind::None, {}, {}};
    if (auto wii = parseWiiIso(header)) return {DiscKind::Wii, wii->id6, std::move(wii->title)};
    if (auto gc = parseGcIso(header)) return {DiscKind::Gc, gc->id6, std::move(gc->title)};
    if (header.size() >= 0x20 && validId6Chars(header)) {
        const std::string id6(reinterpret_cast<const char*>(header.data()), 6);
        if (be32(header, 0x18) == kWiiMagic) return {DiscKind::Wii, id6, {}};
        if (be32(header, 0x1C) == kGcMagic) return {DiscKind::Gc, id6, {}};
    }
    return {DiscKind::Unknown, {}, {}};
}

std::optional<OpticalDisc> parseFakeDisc(std::string_view spec) {
    spec = trim(spec);
    if (spec.empty()) return std::nullopt;
    DiscKind kind = DiscKind::Wii;
    if (ieqPrefix(spec, "gc:")) {
        kind = DiscKind::Gc;
        spec.remove_prefix(3);
    }
    if (spec.size() != 6) return std::nullopt;
    std::string id6;
    id6.reserve(6);
    for (char c : spec) {
        const char u = upperAscii(c);
        if (!((u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9'))) return std::nullopt;
        id6.push_back(u);
    }
    return OpticalDisc{kind, std::move(id6), "Fake Disc " + id6};
}

}  // namespace nl
