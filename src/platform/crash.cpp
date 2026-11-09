#include "platform/crash.hpp"

#include <cstring>
#include <sys/iosupport.h>

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <tuxedo/ppc/exception.h>

#include "core/assert.hpp"
#include "core/crash_report.hpp"
#include "platform/log.hpp"

extern "C" void __reload(void);

namespace nl::platform {

namespace {

constexpr u32 kReturnAfterSec = 30;

// Nur ausgerichtete Adressen in MEM1 und MEM2 (gecachter Bereich) gelten als lesbar.
bool readWord(u32 addr, u32& out) {
    if ((addr & 3u) != 0) return false;
    const bool mem1 = addr >= 0x80000000u && addr < 0x81800000u;
    const bool mem2 = addr >= 0x90000000u && addr < 0x94000000u;
    if (!mem1 && !mem2) return false;
    out = *reinterpret_cast<const volatile u32*>(addr);
    return true;
}

u32 readDar() {
    u32 v;
    asm volatile("mfspr %0, 19" : "=r"(v));
    return v;
}

u32 readDsisr() {
    u32 v;
    asm volatile("mfspr %0, 18" : "=r"(v));
    return v;
}

char g_report[2048];

// Schreibt direkt über das Konsolen-Device statt über stdio: printf könnte beim ersten Aufruf einen
// Puffer per malloc anlegen – auf einem kaputten Heap (häufigste Absturzursache) hinge der Handler.
void writeConsole(const char* text) {
    const devoptab_t* dev = devoptab_list[STD_OUT];
    if (dev == nullptr || dev->write_r == nullptr) return;
    dev->write_r(_REENT, nullptr, text, std::strlen(text));
}

[[noreturn]] void waitAndReturn() {
    const u64 start = gettime();
    while (true) {
        PAD_ScanPads();
        bool start_pressed = false;
        for (int i = 0; i < 4; ++i) start_pressed |= (PAD_ButtonsDown(i) & PAD_BUTTON_START) != 0;
        if (SYS_ResetButtonDown() || start_pressed) break;
        if (ticks_to_secs(diff_ticks(start, gettime())) >= kReturnAfterSec) break;
    }
    __reload();
    while (true) {
    }
}

void onException(unsigned exid, PPCContext* ctx) {
    CrashInfo info;
    info.exceptionId = exid;
    info.pc = ctx->pc;
    info.lr = ctx->lr;
    info.msr = ctx->msr;
    info.dar = readDar();
    info.dsisr = readDsisr();
    for (int i = 0; i < 32; ++i) info.gpr[i] = ctx->gpr[i];
    // Zuerst ins Log: der Dolphin-Smoke-Test erkennt den Absturz daran, auch wenn die Anzeige scheitert.
    log::line("NL_CRASH id=%u pc=%08X lr=%08X", exid, static_cast<unsigned>(info.pc), static_cast<unsigned>(info.lr));

    u32 trace[16];
    const int n = backtrace(info.gpr[1], &readWord, trace);
    formatCrashReport(info, std::span<const u32>(trace, static_cast<std::size_t>(n)), g_report, sizeof g_report);

    GX_AbortFrame();
    GXRModeObj* mode = VIDEO_GetPreferredMode(nullptr);
    void* fb = VIDEO_GetCurrentFramebuffer();
    if (fb != nullptr) {
        VIDEO_ClearFrameBuffer(mode, fb, COLOR_BLACK);
        CON_Init(fb, 24, 24, mode->fbWidth - 48, mode->xfbHeight - 48, mode->fbWidth * VI_DISPLAY_PIX_SZ);
        writeConsole("newlauncher ist abgestuerzt.\n\n");
        writeConsole(g_report);
        writeConsole("\nRESET oder START: zurueck zum Homebrew Channel\n");
    }
    waitAndReturn();
}

[[noreturn]] void onPanic(const char* expr, const char* file, int line) {
    log::line("NL_PANIC %s %s:%d", expr, file, line);
    __builtin_trap();  // landet im selben Absturzbildschirm
}

}  // namespace

void installCrashHandler() {
    PPCExcptCurPanicFn = &onException;
    setPanicHandler(&onPanic);
}

}  // namespace nl::platform
