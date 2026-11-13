#include "platform/system.hpp"

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/machine/processor.h>

#include <algorithm>
#include <cstring>

#include "core/sha256.hpp"

namespace nl::platform {

namespace {
constexpr unsigned kHwAhbprot = 0x0D800064;  // Hollywood: AHB-Schutz; alle Bits gesetzt = Zugriff frei
}

bool ahbprotDisabled() { return read32(kHwAhbprot) == 0xFFFFFFFFu; }

void randomBytes(std::span<u8> out) {
    Sha256 h;
    for (int i = 0; i < 64; ++i) {
        u64 t = gettime();
        // Warten, bis sich der Zeitgeber bewegt hat; die Zahl der Runden schwankt mit Caches und Interrupts.
        volatile u32 spins = 0;
        while (gettime() == t) spins = spins + 1;
        t ^= static_cast<u64>(spins) << 32;
        u8 b[8];
        for (int k = 0; k < 8; ++k) b[k] = static_cast<u8>(t >> (8 * k));
        h.update(b);
    }
    const std::array<u8, 32> d = h.finish();
    std::copy_n(d.begin(), std::min(out.size(), d.size()), out.begin());
}

std::optional<WiiParental> readWiiParental() {
    constexpr std::size_t kSize = 0x4000;
    alignas(32) static u8 buf[kSize];
    if (ISFS_Initialize() < 0) return std::nullopt;  // nand_titles hält ISFS offen; dann ein No-Op
    const s32 fd = ISFS_Open("/shared2/sys/SYSCONF", ISFS_OPEN_READ);
    if (fd < 0) return std::nullopt;
    const s32 got = ISFS_Read(fd, buf, kSize);
    ISFS_Close(fd);
    if (got != static_cast<s32>(kSize)) return std::nullopt;
    const auto item = sysconfItem(std::span<const u8>(buf, kSize), "IPL.PC");
    std::optional<WiiParental> pc = item ? parseIplPc(*item) : std::nullopt;
    std::memset(buf, 0, kSize);  // PIN nicht im Speicher liegen lassen
    return pc;
}

}  // namespace nl::platform
