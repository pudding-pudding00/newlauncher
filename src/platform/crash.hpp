#pragma once
// Absturzbildschirm (Spec 4.2): CPU-Ausnahmen und verletzte NL_ASSERTs zeigen Typ, Register und
// Backtrace; RESET, GC-Start oder 30 s Wartezeit führen zurück zum Homebrew Channel.

namespace nl::platform {

void installCrashHandler();

}  // namespace nl::platform
