#include "launch/chainloader.hpp"

#include <gccore.h>
#include <ogc/machine/processor.h>

#include <cstring>
#include <span>

#include "app_booter_bin.h"
#include "core/launch_layout.hpp"

namespace nl::launch {

namespace {

inline void* at(u32 addr) { return reinterpret_cast<void*>(addr); }

}  // namespace

Result<void> loadDolToBuffer(FileSystem& fs, const LaunchPlan& plan) {
    if (plan.dolSize == 0 || plan.dolSize > kMaxDolBytes) {
        return Result<void>::err({ErrorCode::InvalidData, plan.dolPath});
    }
    auto* buf = static_cast<u8*>(at(kDolBufferAddr));
    auto n = fs.read(plan.dolPath, 0, std::span<u8>(buf, plan.dolSize));
    if (!n.isOk()) return Result<void>::err(n.error());
    if (n.value() != plan.dolSize) return Result<void>::err({ErrorCode::InvalidData, plan.dolPath});
    return Result<void>::ok();
}

void chainload(const LaunchPlan& plan) {
    // Kopf für den Booter direkt vor dem argv-Block: {dolAddr, dolSize, argvAddr, argvSize}
    auto* head = static_cast<u32*>(at(kArgsAddr - 0x10));
    head[0] = kDolBufferAddr;
    head[1] = plan.dolSize;
    head[2] = kArgsAddr;
    head[3] = static_cast<u32>(plan.argvBlock.size());
    std::memcpy(at(kArgsAddr), plan.argvBlock.data(), plan.argvBlock.size());
    std::memcpy(at(kBooterAddr), app_booter_bin, app_booter_bin_size);

    // Alles aus dem Datencache in den Speicher, Befehlscache für den Booter-Code verwerfen.
    DCFlushRange(at(kDolBufferAddr), plan.dolSize);
    DCFlushRange(at(kBooterAddr), kReservedEnd - kBooterAddr);
    ICInvalidateRange(at(kBooterAddr), app_booter_bin_size);

    SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
    u32 level = 0;
    _CPU_ISR_Disable(level);
    static_cast<void>(level);
    reinterpret_cast<void (*)()>(kBooterAddr)();
    for (;;) {
    }
}

}  // namespace nl::launch
