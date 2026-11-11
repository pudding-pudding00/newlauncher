// Ersatz für __exception_closeall() aus dem alten libogc (siehe boot/UPSTREAM.md, „Anpassungen“).
// Das heutige libogc (tuxedo) hat die Funktion nicht mehr; der Booter ruft sie direkt vor dem Sprung ins
// Spiel. Wie früher: externe Interrupts aus, dann jeder Ausnahmevektor nur noch „rfi“. Das Spiel setzt
// seine eigenen Vektoren beim OSInit.
#include <gccore.h>
#include <ogc/machine/processor.h>

namespace {
constexpr u32 kVectors[] = {0x0100, 0x0200, 0x0300, 0x0400, 0x0500, 0x0600, 0x0700, 0x0800,
                            0x0900, 0x0C00, 0x0D00, 0x0F00, 0x1300, 0x1400, 0x1700};
constexpr u32 kRfi = 0x4C000064;
}  // namespace

extern "C" void __exception_closeall() {
    mtmsr(mfmsr() & ~MSR_EE);
    mtmsr(mfmsr() | (MSR_FP | MSR_RI));
    for (u32 v : kVectors) {
        auto* p = reinterpret_cast<u32*>(0x80000000u | v);
        *p = kRfi;
        DCFlushRangeNoSync(p, 0x100);
        ICInvalidateRange(p, 0x100);
    }
    _sync();
}
