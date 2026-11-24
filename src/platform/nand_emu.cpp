// Portiert und erweitert aus Fledge68/WiiFlow_Lite (Commit 69c0d4d):
// - source/channel/nand.cpp (Zlib-Lizenz, Miigotu 2011 / OverjoY 2012): d2x fat-module (0xF0..0xF3) und /dev/fs (100)
// - source/channel/channel_launcher.cpp (GPL-3.0-or-later): Identify / ES_Identify für EmuNAND-Kanäle
// Änderungen gegenüber WiiFlow Lite: kein static-Initialisierungs-Bug bei mode, explizite DCFlushRange-Aufrufe,
// C++-RAII für ausgerichtete ISFS-Puffer und Rückfall auf die vorher gelesenen Zertifikate des echten NAND.

#include "platform/nand_emu.hpp"

#include <gccore.h>
#include <malloc.h>
#include <ogc/cache.h>
#include <ogc/es.h>
#include <ogc/ipc.h>
#include <ogc/isfs.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "platform/log.hpp"

namespace nl::platform {

namespace {

constexpr u32 kIoctlFatMountSd = 0xF0;
constexpr u32 kIoctlFatUnmountSd = 0xF1;
constexpr u32 kIoctlFatMountUsb = 0xF2;
constexpr u32 kIoctlFatUnmountUsb = 0xF3;
constexpr u32 kIoctlIsfsSetMode = 100;
constexpr u32 kModeSd = 0x01;
constexpr u32 kModeUsb = 0x02;
constexpr u32 kModeFull = 0x100;
constexpr std::size_t kMaxIsfsFile = 2 * 1024 * 1024;

struct AlignedFree {
    void operator()(void* p) const { std::free(p); }
};
using AlignedBuf = std::unique_ptr<u8, AlignedFree>;

AlignedBuf alignedAlloc(std::size_t bytes) {
    const std::size_t rounded = (bytes + 31) & ~std::size_t{31};
    return AlignedBuf(static_cast<u8*>(memalign(32, rounded == 0 ? 32 : rounded)));
}

alignas(32) ioctlv g_vec[2];
alignas(32) s32 g_part = 0;
alignas(32) u32 g_mode = 0;
alignas(32) char g_nandPath[96]{};

bool readIsfsFile(const char* path, AlignedBuf& outBuf, u32& outSize) {
    outBuf.reset();
    outSize = 0;
    const s32 fd = ISFS_Open(path, ISFS_OPEN_READ);
    if (fd < 0) return false;
    alignas(32) fstats st{};
    DCFlushRange(&st, sizeof st);
    const s32 statRc = ISFS_GetFileStats(fd, &st);
    if (statRc < 0 || st.file_length == 0 || st.file_length > kMaxIsfsFile) {
        ISFS_Close(fd);
        return false;
    }
    AlignedBuf buf = alignedAlloc(st.file_length);
    if (!buf) {
        ISFS_Close(fd);
        return false;
    }
    const s32 got = ISFS_Read(fd, buf.get(), st.file_length);
    ISFS_Close(fd);
    if (got != static_cast<s32>(st.file_length)) return false;
    outSize = st.file_length;
    outBuf = std::move(buf);
    return true;
}

}  // namespace

std::vector<u8> readRealNandCerts() {
    AlignedBuf buf;
    u32 size = 0;
    if (!readIsfsFile("/sys/cert.sys", buf, size)) return {};
    return std::vector<u8>(buf.get(), buf.get() + size);
}

bool enableNandEmu(const EmuNandPath& path, bool usb, s8 mbrIndex, bool full) {
    if (path.folder.empty() || path.folder.size() > kMaxEmuNandFolderLen) return false;

    const s32 fatFd = IOS_Open("fat", 0);
    if (fatFd < 0) {
        log::line("NL_EMUNAND_ERR open_fat=%d", static_cast<int>(fatFd));
        return false;
    }
    g_part = mbrIndex >= 0 ? static_cast<s32>(mbrIndex) : 0;
    DCFlushRange(&g_part, 32);
    g_vec[0].data = &g_part;
    g_vec[0].len = sizeof(g_part);
    const s32 fatRc = IOS_Ioctlv(fatFd, usb ? kIoctlFatMountUsb : kIoctlFatMountSd, 1, 0, g_vec);
    IOS_Close(fatFd);
    if (fatRc < 0) {
        log::line("NL_EMUNAND_ERR fat=%d", static_cast<int>(fatRc));
        return false;
    }

    const s32 fsFd = IOS_Open("/dev/fs", 0);
    if (fsFd < 0) {
        log::line("NL_EMUNAND_ERR open_fs=%d", static_cast<int>(fsFd));
        disableNandEmu();
        return false;
    }
    g_mode = (usb ? kModeUsb : kModeSd) | (full ? kModeFull : 0u);
    std::memset(g_nandPath, 0, sizeof g_nandPath);
    std::memcpy(g_nandPath, path.folder.data(), path.folder.size());
    DCFlushRange(&g_mode, 32);
    DCFlushRange(g_nandPath, sizeof g_nandPath);
    g_vec[0].data = &g_mode;
    g_vec[0].len = sizeof(g_mode);
    g_vec[1].data = g_nandPath;
    g_vec[1].len = static_cast<u32>(path.folder.size() + 1);
    const s32 fsRc = IOS_Ioctlv(fsFd, kIoctlIsfsSetMode, 2, 0, g_vec);
    IOS_Close(fsFd);
    if (fsRc < 0) {
        log::line("NL_EMUNAND_ERR fs=%d", static_cast<int>(fsRc));
        disableNandEmu();
        return false;
    }
    return true;
}

void disableNandEmu() {
    const s32 fsFd = IOS_Open("/dev/fs", 0);
    if (fsFd >= 0) {
        g_mode = 0;
        DCFlushRange(&g_mode, 32);
        IOS_Ioctl(fsFd, kIoctlIsfsSetMode, &g_mode, sizeof(g_mode), nullptr, 0);
        IOS_Close(fsFd);
    }
    const s32 fatFd = IOS_Open("fat", 0);
    if (fatFd >= 0) {
        IOS_Ioctlv(fatFd, kIoctlFatUnmountSd, 0, 0, nullptr);
        IOS_Ioctlv(fatFd, kIoctlFatUnmountUsb, 0, 0, nullptr);
        IOS_Close(fatFd);
    }
}

bool identifyEmuChannel(u64 titleId, std::span<const u8> fallbackCerts) {
    if (ISFS_Initialize() < 0) return false;
    const unsigned hi = static_cast<unsigned>(titleId >> 32);
    const unsigned lo = static_cast<unsigned>(titleId & 0xFFFFFFFFu);

    char tmdPath[64];
    char tikPath[64];
    std::snprintf(tmdPath, sizeof tmdPath, "/title/%08x/%08x/content/title.tmd", hi, lo);
    std::snprintf(tikPath, sizeof tikPath, "/ticket/%08x/%08x.tik", hi, lo);

    AlignedBuf tmd, tik, certs;
    u32 tmdSize = 0, tikSize = 0, certsSize = 0;
    const bool gotTmd = readIsfsFile(tmdPath, tmd, tmdSize);
    const bool gotTik = gotTmd && readIsfsFile(tikPath, tik, tikSize);
    bool gotCerts = gotTik && readIsfsFile("/sys/cert.sys", certs, certsSize);
    if (!gotCerts && gotTik && !fallbackCerts.empty()) {
        certs = alignedAlloc(fallbackCerts.size());
        if (certs) {
            std::memcpy(certs.get(), fallbackCerts.data(), fallbackCerts.size());
            certsSize = static_cast<u32>(fallbackCerts.size());
            gotCerts = true;
        }
    }
    ISFS_Deinitialize();
    if (!gotTmd || !gotTik || !gotCerts) {
        log::line("NL_EMUNAND_ERR files tmd=%d tik=%d certs=%d", gotTmd ? 1 : 0, gotTik ? 1 : 0, gotCerts ? 1 : 0);
        return false;
    }

    DCFlushRange(tmd.get(), tmdSize);
    DCFlushRange(tik.get(), tikSize);
    DCFlushRange(certs.get(), certsSize);
    u32 keyId = 0;
    const s32 rc = ES_Identify(reinterpret_cast<const signed_blob*>(certs.get()), certsSize,
                               reinterpret_cast<const signed_blob*>(tmd.get()), tmdSize,
                               reinterpret_cast<const signed_blob*>(tik.get()), tikSize, &keyId);
    if (rc < 0) {
        log::line("NL_EMUNAND_ERR identify=%d", static_cast<int>(rc));
        return false;
    }
    return true;
}

}  // namespace nl::platform
