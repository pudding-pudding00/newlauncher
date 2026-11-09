#pragma once
// Interne Invarianten (Spec 4.2): ein verletztes NL_ASSERT ist ein Programmfehler und endet im
// Panic-Handler. Auf der Wii führt der zum Absturzbildschirm, im Host-Test zu einem Sprung zurück.

namespace nl {

using PanicHandler = void (*)(const char* expr, const char* file, int line);

// nullptr stellt den Standard (abort) wieder her.
void setPanicHandler(PanicHandler handler);

[[noreturn]] void panic(const char* expr, const char* file, int line);

}  // namespace nl

#define NL_ASSERT(cond) ((cond) ? (void)0 : ::nl::panic(#cond, __FILE__, __LINE__))
