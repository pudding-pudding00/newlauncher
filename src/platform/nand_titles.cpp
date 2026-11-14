#include "platform/nand_titles.hpp"

#include <gccore.h>
#include <malloc.h>

#include <algorithm>
#include <cstdio>
#include <memory>

#include "core/text_fold.hpp"

namespace nl::platform {

namespace {

struct AlignedFree {
    void operator()(void* p) const { std::free(p); }
};
using AlignedBuf = std::unique_ptr<u8, AlignedFree>;

// ES/ISFS verlangen 32-Byte-ausgerichtete Puffer in Vielfachen von 32.
AlignedBuf alignedAlloc(std::size_t bytes) {
    const std::size_t rounded = (bytes + 31) & ~std::size_t{31};
    return AlignedBuf(static_cast<u8*>(memalign(32, rounded == 0 ? 32 : rounded)));
}

}  // namespace

std::optional<u32> bannerContentId(u64 titleId) {
    u32 size = 0;
    if (ES_GetStoredTMDSize(titleId, &size) < 0 || size == 0) return std::nullopt;
    AlignedBuf buf = alignedAlloc(size);
    if (!buf) return std::nullopt;
    if (ES_GetStoredTMD(titleId, reinterpret_cast<signed_blob*>(buf.get()), size) < 0) return std::nullopt;
    auto tmd = parseTmd(std::span<const u8>(buf.get(), size));
    if (!tmd.isOk()) return std::nullopt;
    return contentIdForIndex(tmd.value(), 0);
}

namespace {

std::string channelName(u64 titleId, BannerLang lang) {
    const std::optional<u32> cid = bannerContentId(titleId);
    if (!cid) return {};
    char path[64];
    std::snprintf(path, sizeof path, "/title/%08x/%08x/content/%08x.app", static_cast<unsigned>(titleId >> 32),
                  static_cast<unsigned>(titleId & 0xFFFFFFFFu), static_cast<unsigned>(*cid));
    const s32 fd = ISFS_Open(path, ISFS_OPEN_READ);
    if (fd < 0) return {};
    AlignedBuf buf = alignedAlloc(kImetReadBytes);
    const s32 got = buf ? ISFS_Read(fd, buf.get(), kImetReadBytes) : -1;
    ISFS_Close(fd);
    if (got <= 0) return {};
    return imetChannelName(std::span<const u8>(buf.get(), static_cast<std::size_t>(got)), lang);
}

}  // namespace

std::vector<u64> installedTitleIds() {
    u32 count = 0;
    if (ES_GetNumTitles(&count) < 0 || count == 0) return {};
    AlignedBuf buf = alignedAlloc(count * sizeof(u64));
    if (!buf) return {};
    auto* ids = reinterpret_cast<u64*>(buf.get());
    if (ES_GetTitles(ids, count) < 0) return {};
    std::vector<u64> out;
    for (u32 i = 0; i < count; ++i)
        if (isChannelTitle(ids[i])) out.push_back(ids[i]);
    return out;
}

std::vector<ChannelInfo> loadChannelNames(std::span<const u64> ids, BannerLang lang) {
    static bool isfsReady = false;
    if (!isfsReady) isfsReady = ISFS_Initialize() >= 0;
    std::vector<ChannelInfo> out;
    out.reserve(ids.size());
    for (u64 id : ids) out.push_back({id, isfsReady ? channelName(id, lang) : std::string()});
    std::sort(out.begin(), out.end(), [](const ChannelInfo& a, const ChannelInfo& b) {
        return foldForSearch(channelDisplayName(a)) < foldForSearch(channelDisplayName(b));
    });
    return out;
}

BannerLang systemBannerLang() {
    const s32 lang = CONF_GetLanguage();
    if (lang >= CONF_LANG_JAPANESE && lang <= CONF_LANG_DUTCH) return static_cast<BannerLang>(lang);
    return BannerLang::English;
}

const char* systemCoverRegion() {
    switch (CONF_GetLanguage()) {
        case CONF_LANG_GERMAN: return "DE";
        case CONF_LANG_FRENCH: return "FR";
        case CONF_LANG_JAPANESE: return "JA";
        default: return "EN";
    }
}

}  // namespace nl::platform
