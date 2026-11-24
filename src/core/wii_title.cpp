#include "core/wii_title.hpp"

#include "core/utf8.hpp"

namespace nl {

namespace {

constexpr std::size_t kTmdTitleId = 0x18C;
constexpr std::size_t kTmdTitleVersion = 0x1DC;
constexpr std::size_t kTmdNumContents = 0x1DE;
constexpr std::size_t kTmdBootIndex = 0x1E0;
constexpr std::size_t kTmdContents = 0x1E4;
constexpr std::size_t kTmdContentSize = 36;

constexpr std::size_t kImetMagic = 0x40;
constexpr std::size_t kImetMagicAlt = 0x80;
constexpr std::size_t kImetNameOffset = 0x1C;  // Namen stehen direkt hinter 28-Byte-IMET-Header (0x40->0x5C, 0x80->0x9C)
constexpr std::size_t kImetNameUnits = 42;
constexpr std::size_t kImetLangCount = 10;
constexpr std::size_t kImetTableBytes = kImetNameOffset + kImetLangCount * kImetNameUnits * 2;

u16 be16(std::span<const u8> d, std::size_t at) { return static_cast<u16>((d[at] << 8) | d[at + 1]); }
u32 be32(std::span<const u8> d, std::size_t at) { return (static_cast<u32>(be16(d, at)) << 16) | be16(d, at + 2); }
u64 be64(std::span<const u8> d, std::size_t at) { return (static_cast<u64>(be32(d, at)) << 32) | be32(d, at + 4); }

Result<TmdInfo> invalid(std::string context) { return Result<TmdInfo>::err({ErrorCode::InvalidData, std::move(context)}); }

bool hasImetAt(std::span<const u8> d, std::size_t off) {
    return d.size() >= off + kImetTableBytes && d[off] == 'I' && d[off + 1] == 'M' && d[off + 2] == 'E' &&
           d[off + 3] == 'T';
}

std::string decodeName(std::span<const u8> d, std::size_t namesBase, std::size_t lang) {
    const std::size_t base = namesBase + lang * kImetNameUnits * 2;
    std::string out;
    for (std::size_t i = 0; i < kImetNameUnits; ++i) {
        const u16 u = be16(d, base + i * 2);
        if (u == 0) break;
        if (u >= 0xD800 && u <= 0xDBFF && i + 1 < kImetNameUnits) {
            const u16 lo = be16(d, base + (i + 1) * 2);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                appendUtf8(out, 0x10000u + ((static_cast<u32>(u) - 0xD800u) << 10) + (lo - 0xDC00u));
                ++i;
                continue;
            }
        }
        appendUtf8(out, u);  // unpaarige Surrogate werden dort zu U+FFFD
    }
    return out;
}

}  // namespace

bool isChannelTitle(u64 titleId) { return static_cast<u32>(titleId >> 32) == kChannelTitleType; }

Result<TmdInfo> parseTmd(std::span<const u8> d) {
    if (d.size() < kTmdContents) return invalid("TMD zu kurz");
    TmdInfo t;
    t.titleId = be64(d, kTmdTitleId);
    t.titleVersion = be16(d, kTmdTitleVersion);
    t.bootIndex = be16(d, kTmdBootIndex);
    const std::size_t n = be16(d, kTmdNumContents);
    if (d.size() < kTmdContents + n * kTmdContentSize) return invalid("TMD-Contents abgeschnitten");
    t.contents.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t at = kTmdContents + i * kTmdContentSize;
        t.contents.push_back({be32(d, at), be16(d, at + 4), be16(d, at + 6), be64(d, at + 8)});
    }
    return Result<TmdInfo>::ok(std::move(t));
}

std::optional<u32> contentIdForIndex(const TmdInfo& tmd, u16 index) {
    for (const TmdContent& c : tmd.contents)
        if (c.index == index) return c.id;
    return std::nullopt;
}

std::string imetChannelName(std::span<const u8> d, BannerLang lang) {
    const std::size_t magic = hasImetAt(d, kImetMagic) ? kImetMagic
                            : hasImetAt(d, kImetMagicAlt) ? kImetMagicAlt
                                                          : 0;
    if (magic == 0) return {};
    const std::size_t namesBase = magic + kImetNameOffset;
    if (std::string s = decodeName(d, namesBase, static_cast<std::size_t>(lang)); !s.empty()) return s;
    if (std::string s = decodeName(d, namesBase, static_cast<std::size_t>(BannerLang::English)); !s.empty()) return s;
    for (std::size_t l = 0; l < kImetLangCount; ++l)
        if (std::string s = decodeName(d, namesBase, l); !s.empty()) return s;
    return {};
}

std::optional<u32> imetArchiveOffset(std::span<const u8> d) {
    auto imetAt = [&](std::size_t at) {
        return d.size() >= at + 4 && d[at] == 'I' && d[at + 1] == 'M' && d[at + 2] == 'E' && d[at + 3] == 'T';
    };
    if (imetAt(0x40)) return 0x600u;
    if (imetAt(0x80)) return 0x640u;
    return std::nullopt;
}

}  // namespace nl
