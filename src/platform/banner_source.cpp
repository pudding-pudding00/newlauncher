#include "platform/banner_source.hpp"

#include <gccore.h>
#include <malloc.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "core/wii_partition.hpp"
#include "platform/nand_titles.hpp"

namespace nl::platform {

namespace {
struct AlignedFree {
    void operator()(void* p) const { std::free(p); }
};
Result<std::vector<u8>> fail(ErrorCode code, const char* what) { return Result<std::vector<u8>>::err(Error{code, what}); }
// Titelschlüssel über ES; IOS verlangt 32-Byte-ausgerichtete Puffer. Nur im Banner-Thread, einer zugleich.
std::optional<AesBlock> esTitleKey(const WiiTicketKey& k) {
    alignas(32) static u32 iv[4];
    alignas(32) static u8 in[32];
    alignas(32) static u8 out[32];
    std::memcpy(iv, k.iv.data(), 16);
    std::memcpy(in, k.encrypted.data(), 16);
    const u32 keynum = k.index == 1 ? ES_KEY_KOREAN : ES_KEY_COMMON;
    if (ES_Decrypt(keynum, iv, in, 16, out) < 0) return std::nullopt;
    AesBlock key{};
    std::memcpy(key.data(), out, 16);
    return key;
}
}  // namespace

Result<std::vector<u8>> readWiiDiscBanner(FileSystem& fs, const std::string& gamePath) {
    auto disc = openWiiDiscFile(fs, gamePath);
    if (!disc.isOk()) return Result<std::vector<u8>>::err(disc.error());
    return readWiiOpeningBnr(*disc.value(), esTitleKey, kWiiBannerMax);
}

Result<std::vector<u8>> readChannelContent0(u64 titleId) {
    static bool isfsReady = false;
    if (!isfsReady) isfsReady = ISFS_Initialize() >= 0;
    if (!isfsReady) return fail(ErrorCode::IoError, "ISFS");
    const std::optional<u32> cid = bannerContentId(titleId);
    if (!cid) return fail(ErrorCode::NotFound, "TMD");
    char path[64];
    std::snprintf(path, sizeof path, "/title/%08x/%08x/content/%08x.app", static_cast<unsigned>(titleId >> 32),
                  static_cast<unsigned>(titleId & 0xFFFFFFFFu), static_cast<unsigned>(*cid));
    const s32 fd = ISFS_Open(path, ISFS_OPEN_READ);
    if (fd < 0) return fail(ErrorCode::NotFound, "Content 0");
    alignas(32) static fstats stats;  // ISFS will 32-Byte-ausgerichtete Puffer; nur im Banner-Thread benutzt
    if (ISFS_GetFileStats(fd, &stats) < 0) {
        ISFS_Close(fd);
        return fail(ErrorCode::IoError, "Content 0: Größe");
    }
    const u32 size = stats.file_length;
    if (size == 0 || size > kChannelContent0Max) {
        ISFS_Close(fd);
        return fail(ErrorCode::InvalidData, "Content 0: Größe");
    }
    // In Stücken über einen kleinen ausgerichteten Puffer, damit der Inhalt nicht zweimal im Speicher liegt.
    constexpr u32 kChunk = 32 * 1024;
    std::unique_ptr<u8, AlignedFree> chunk(static_cast<u8*>(memalign(32, kChunk)));
    std::vector<u8> out;
    if (chunk) out.resize(size);
    if (!chunk || out.size() != size) {
        ISFS_Close(fd);
        return fail(ErrorCode::IoError, "Content 0: Speicher");
    }
    for (u32 at = 0; at < size;) {
        const u32 want = std::min(kChunk, size - at);
        const s32 got = ISFS_Read(fd, chunk.get(), want);
        if (got <= 0 || static_cast<u32>(got) > want) {
            ISFS_Close(fd);
            return fail(ErrorCode::IoError, "Content 0: Lesen");
        }
        std::memcpy(out.data() + at, chunk.get(), static_cast<std::size_t>(got));
        at += static_cast<u32>(got);
    }
    ISFS_Close(fd);
    return Result<std::vector<u8>>::ok(std::move(out));
}

}  // namespace nl::platform
