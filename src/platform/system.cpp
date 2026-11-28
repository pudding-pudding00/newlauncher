#include "platform/system.hpp"

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/machine/processor.h>

#include <algorithm>
#include <cstring>
#include <ctime>

#include "core/sha256.hpp"

namespace nl::platform {

namespace {
constexpr unsigned kHwAhbprot = 0x0D800064;  // Hollywood: AHB-Schutz; alle Bits gesetzt = Zugriff frei
}

bool ahbprotDisabled() { return read32(kHwAhbprot) == 0xFFFFFFFFu; }

void randomBytes(std::span<u8> out) {
    Sha256 h;
    // Feste Anteile (RTC-Zeit, Geräte-ID) machen Salze verschiedener Konsolen und Tage verschieden, auch wenn
    // der Zeitgeber-Jitter nach dem Einschalten ähnlich ausfällt. Der Jitter bleibt die eigentliche Zufallsquelle.
    const u64 rtc = static_cast<u64>(std::time(nullptr));
    u32 device = 0;
    if (ES_GetDeviceID(&device) < 0) device = 0;
    u8 seed[12];
    for (int k = 0; k < 8; ++k) seed[k] = static_cast<u8>(rtc >> (8 * k));
    for (int k = 0; k < 4; ++k) seed[8 + k] = static_cast<u8>(device >> (8 * k));
    h.update(seed);
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
    // Bewusst ohne ISFS_Deinitialize: ISFS bleibt für die ganze Sitzung offen (nand_titles hält es ebenso);
    // wii_boot schließt es vor dem IOS-Wechsel. Ist es schon offen, ist der Aufruf ein No-Op.
    if (ISFS_Initialize() < 0) return std::nullopt;
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

void writePriiloaderSystemMenuMagic() {
    auto* magic = reinterpret_cast<volatile u32*>(0x8132FFFB);
    *magic = 0x50756E65u;  // "Pune"
    DCFlushRange(reinterpret_cast<void*>(0x8132FFE0), 64);
}

}  // namespace nl::platform
