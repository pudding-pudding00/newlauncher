#pragma once
// Absturzbericht (Spec 4.2): Ausnahmetyp, Register, Backtrace. Plattformfrei und host-getestet;
// platform/crash.cpp füllt CrashInfo aus dem PPC-Kontext und zeigt den Text an.

#include <cstddef>
#include <span>

#include "core/types.hpp"

namespace nl {

struct CrashInfo {
    u32 exceptionId = 0, pc = 0, lr = 0, msr = 0, dar = 0, dsisr = 0;
    u32 gpr[32] = {};
};

const char* exceptionName(u32 id);

using ReadWord = bool (*)(u32 addr, u32& out);  // false = Adresse nicht lesbar

// Folgt der PPC-Back-Chain ab sp: [sp] = nächster Frame, [nächster + 4] = gesicherter LR.
// Stoppt bei sp == 0, nicht ausgerichteten oder unlesbaren Adressen, nicht aufsteigendem Stack
// und vollem Puffer. Liefert die Anzahl der LR-Werte.
int backtrace(u32 sp, ReadWord read, std::span<u32> out);

// Mehrzeiliger Bericht; nullterminiert, kürzt bei kleinem Puffer, liefert die Länge.
int formatCrashReport(const CrashInfo& info, std::span<const u32> trace, char* out, std::size_t cap);

}  // namespace nl
