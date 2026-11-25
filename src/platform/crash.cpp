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
// Ungecachter MEM2-Bereich 64 KiB unter kBooterAddr (0x93300000): außerhalb der DOL (~4,5 MiB ab 0x92800000)
// und außerhalb von HBC/Arena2; überlebt __reload() in den HBC.
constexpr u32 kCrashPersistAddr = 0x932F0000u;
constexpr u32 kCrashMagic = 0x4E4C4352u;  // "NLCR"

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

void saveCrashReport(const char* text) {
    volatile u32* hdr = reinterpret_cast<volatile u32*>(kCrashPersistAddr | 0x40000000u);
    volatile u8* dst = reinterpret_cast<volatile u8*>((kCrashPersistAddr + 8u) | 0x40000000u);
    hdr[0] = 0;
    std::size_t len = std::strlen(text);
    if (len >= sizeof g_report) len = sizeof g_report - 1;
    for (std::size_t i = 0; i < len; ++i) dst[i] = static_cast<u8>(text[i]);
    dst[len] = 0;
    hdr[1] = static_cast<u32>(len);
    hdr[0] = kCrashMagic;
}

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
    saveCrashReport(g_report);

    GX_AbortFrame();
    GXRModeObj* mode = VIDEO_GetPreferredMode(nullptr);
    void* fb = VIDEO_GetCurrentFramebuffer();
    if (fb != nullptr) {
        VIDEO_ClearFrameBuffer(mode, fb, COLOR_BLACK);
        CON_Init(fb, 24, 24, mode->fbWidth - 48, mode->xfbHeight - 48, mode->fbWidth * VI_DISPLAY_PIX_SZ);
        VIDEO_SetBlack(false);
        VIDEO_Flush();
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

void logPreviousCrash() {
    volatile u32* hdr = reinterpret_cast<volatile u32*>(kCrashPersistAddr | 0x40000000u);
    if (hdr[0] != kCrashMagic) return;
    const u32 len = hdr[1];
    hdr[0] = 0;
    if (len == 0 || len >= sizeof g_report) return;
    const volatile u8* src = reinterpret_cast<const volatile u8*>((kCrashPersistAddr + 8u) | 0x40000000u);
    char buf[sizeof g_report];
    for (u32 i = 0; i < len; ++i) buf[i] = static_cast<char>(src[i]);
    buf[len] = '\0';
    char* cur = buf;
    while (*cur != '\0') {
        char* nl = std::strchr(cur, '\n');
        if (nl != nullptr) *nl = '\0';
        if (*cur != '\0') log::line("NL_PREV_CRASH %s", cur);
        if (nl == nullptr) break;
        cur = nl + 1;
    }
}

}  // namespace nl::platform
