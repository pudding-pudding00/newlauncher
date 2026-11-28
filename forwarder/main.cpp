// Standalone UNLA / Priiloader forwarder DOL for newlauncher (M30).
// Mounts SD/USB, validates sd:/apps/newlauncher/boot.dol (or usb:/...), loads it into kDolBufferAddr,
// packs __system_argv with --from-forwarder, unmounts FAT, and jumps to the embedded app_booter.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <fat.h>
#include <gccore.h>
#include <ogc/machine/processor.h>
#include <ogc/system.h>
#include <unistd.h>

#include "app_booter_bin.h"
#include "core/dol.hpp"
#include "core/forwarder_plan.hpp"
#include "core/launch_args.hpp"
#include "core/launch_layout.hpp"

namespace {

inline void* at(nl::u32 addr) { return reinterpret_cast<void*>(addr); }

bool tryLoadDol(const std::string& path, nl::u32& outSize) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return false;
    }
    const long sz = std::ftell(f);
    if (sz <= 0 || static_cast<nl::u64>(sz) > nl::kMaxDolBytes) {
        std::fclose(f);
        return false;
    }
    std::rewind(f);
    auto* dst = static_cast<nl::u8*>(at(nl::kDolBufferAddr));
    const std::size_t got = std::fread(dst, 1, static_cast<std::size_t>(sz), f);
    std::fclose(f);
    if (got != static_cast<std::size_t>(sz)) return false;
    auto chk = nl::checkDolImage(std::span<const nl::u8>(dst, got), nl::kMaxDolBytes);
    if (!chk.isOk()) return false;
    outSize = static_cast<nl::u32>(got);
    return true;
}

}  // namespace

int main() {
    // Lower ArenaHi2 below kDolBufferAddr so libfat / malloc never touches the chainload buffer.
    if (reinterpret_cast<nl::u32>(SYS_GetArena2Hi()) > nl::kDolBufferAddr) {
        SYS_SetArena2Hi(at(nl::kDolBufferAddr));
    }

    fatInitDefault();

    std::string chosenPath;
    nl::u32 dolSize = 0;
    const auto candidates = nl::forwarderCandidates();
    for (int attempt = 0; attempt < 5 && chosenPath.empty(); ++attempt) {
        for (const auto& p : candidates) {
            if (tryLoadDol(p, dolSize)) {
                chosenPath = p;
                break;
            }
        }
        if (chosenPath.empty()) usleep(250 * 1000);
    }

    if (chosenPath.empty()) {
        SYS_Report("NL_FWD missing\n");
        VIDEO_Init();
        GXRModeObj* rmode = VIDEO_GetPreferredMode(nullptr);
        void* xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
        console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * VI_DISPLAY_PIX_SZ);
        VIDEO_Configure(rmode);
        VIDEO_SetNextFramebuffer(xfb);
        VIDEO_SetBlack(FALSE);
        VIDEO_Flush();
        VIDEO_WaitVSync();
        std::printf("\n\n  newlauncher forwarder (UNLA)\n\n");
        std::printf("  boot.dol not found / nicht gefunden:\n");
        std::printf("    sd:/apps/newlauncher/boot.dol\n");
        std::printf("    usb:/apps/newlauncher/boot.dol\n");
        sleep(5);
        return 0;
    }

    const bool fromSd = chosenPath.starts_with("sd:");
    SYS_Report("NL_FWD boot=%s\n", fromSd ? "sd" : "usb");

    const auto args = nl::forwarderArgv(chosenPath);
    auto argvBlk = nl::packArgvBlock(args, nl::kArgsAddr);
    if (!argvBlk.isOk()) {
        SYS_Report("NL_FWD argv_fail\n");
        return 1;
    }

    fatUnmount("sd");
    fatUnmount("usb");

    auto* head = static_cast<nl::u32*>(at(nl::kArgsAddr - 0x10));
    head[0] = nl::kDolBufferAddr;
    head[1] = dolSize;
    head[2] = nl::kArgsAddr;
    head[3] = static_cast<nl::u32>(argvBlk.value().size());
    std::memcpy(at(nl::kArgsAddr), argvBlk.value().data(), argvBlk.value().size());
    std::memcpy(at(nl::kBooterAddr), app_booter_bin, app_booter_bin_size);

    DCFlushRange(at(nl::kDolBufferAddr), dolSize);
    DCFlushRange(at(nl::kBooterAddr), nl::kReservedEnd - nl::kBooterAddr);
    ICInvalidateRange(at(nl::kBooterAddr), app_booter_bin_size);

    SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
    nl::u32 level = 0;
    _CPU_ISR_Disable(level);
    static_cast<void>(level);
    reinterpret_cast<void (*)()>(nl::kBooterAddr)();
    for (;;) {
    }
}
