#include "core/crash_report.hpp"

#include <cstdarg>
#include <cstdio>

namespace nl {

namespace {
// Hängt formatierten Text an; hält `len` immer < cap und den Puffer nullterminiert.
struct Writer {
    char* out;
    std::size_t cap;
    std::size_t len = 0;

    void add(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        if (cap == 0 || len + 1 >= cap) return;
        va_list args;
        va_start(args, fmt);
        const int n = std::vsnprintf(out + len, cap - len, fmt, args);
        va_end(args);
        if (n < 0) return;
        len += static_cast<std::size_t>(n);
        if (len >= cap) len = cap - 1;
    }
};
}  // namespace

const char* exceptionName(u32 id) {
    switch (id) {
        case 3: return "DSI";
        case 4: return "ISI";
        case 6: return "Alignment";
        case 7: return "Program";
        case 8: return "FPU";
        default: return "Unbekannt";
    }
}

int backtrace(u32 sp, ReadWord read, std::span<u32> out) {
    int n = 0;
    u32 cur = sp;
    while (n < static_cast<int>(out.size())) {
        if (cur == 0 || (cur & 3u) != 0) break;
        u32 next = 0, lr = 0;
        if (!read(cur, next) || next == 0 || (next & 3u) != 0 || next <= cur) break;
        if (!read(next + 4, lr)) break;
        out[n++] = lr;
        cur = next;
    }
    return n;
}

int formatCrashReport(const CrashInfo& info, std::span<const u32> trace, char* out, std::size_t cap) {
    if (cap == 0) return 0;
    out[0] = '\0';
    Writer w{out, cap};
    w.add("Absturz: %s (%u)\n", exceptionName(info.exceptionId), static_cast<unsigned>(info.exceptionId));
    w.add("PC  %08X  LR    %08X  MSR %08X\n", static_cast<unsigned>(info.pc), static_cast<unsigned>(info.lr),
          static_cast<unsigned>(info.msr));
    w.add("DAR %08X  DSISR %08X\n\n", static_cast<unsigned>(info.dar), static_cast<unsigned>(info.dsisr));
    for (int i = 0; i < 32; i += 4) {
        w.add("r%02d %08X  r%02d %08X  r%02d %08X  r%02d %08X\n", i, static_cast<unsigned>(info.gpr[i]), i + 1,
              static_cast<unsigned>(info.gpr[i + 1]), i + 2, static_cast<unsigned>(info.gpr[i + 2]), i + 3,
              static_cast<unsigned>(info.gpr[i + 3]));
    }
    w.add("\nBacktrace:");
    for (std::size_t i = 0; i < trace.size(); ++i) w.add("%s%08X", (i % 6 == 0) ? "\n  " : " ", static_cast<unsigned>(trace[i]));
    w.add("\n");
    return static_cast<int>(w.len);
}

}  // namespace nl
