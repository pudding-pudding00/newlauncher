#include "platform/system_probe.hpp"

#include <gccore.h>
#include <malloc.h>
#include <ogc/machine/processor.h>

#include <cstdlib>
#include <memory>
#include <span>

#include "core/wii_title.hpp"
#include "platform/system.hpp"

namespace nl::platform {

namespace {
constexpr u64 kSystemMenuTitle = 0x0000000100000002ull;
constexpr unsigned kHwChipRevId = 0x0D8005A0;  // Latte (vWii): obere 16 Bit 0xCAFE

struct AlignedFree {
    void operator()(void* p) const { std::free(p); }
};
}  // namespace

u16 sysMenuVersion() {
    u32 size = 0;
    if (ES_GetStoredTMDSize(kSystemMenuTitle, &size) < 0 || size == 0) return 0;
    const std::size_t rounded = (size + 31) & ~u32{31};
    std::unique_ptr<u8, AlignedFree> buf(static_cast<u8*>(memalign(32, rounded)));
    if (!buf) return 0;
    if (ES_GetStoredTMD(kSystemMenuTitle, reinterpret_cast<signed_blob*>(buf.get()), size) < 0) return 0;
    auto tmd = parseTmd(std::span<const u8>(buf.get(), size));
    return tmd.isOk() ? tmd.value().titleVersion : 0;
}

bool latteDetected() {
    if (!ahbprotDisabled()) return false;  // ohne AHB-Zugriff ist das Register nicht lesbar
    return (read32(kHwChipRevId) >> 16) == 0xCAFE;
}

u32 runningIos() {
    const s32 v = IOS_GetVersion();
    return v > 0 ? static_cast<u32>(v) : 0;
}

}  // namespace nl::platform
