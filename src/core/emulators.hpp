#pragma once
// emulators.ini (Spec 5.4): welcher Emulator-DOL eine Konsole startet und mit welchem Argument-Schema.
// Fehlende Schlüssel kommen aus den Standardwerten; N64 über forN64, GC über nintendont(), Wii nicht hier.

#include <array>
#include <optional>
#include <string>

#include "core/console.hpp"
#include "core/ini.hpp"
#include "core/n64.hpp"

namespace nl {

struct EmulatorEntry {
    std::string dol;
    std::string args;
};

class EmulatorTable {
public:
    static EmulatorTable fromIni(const IniDoc& doc);
    const EmulatorEntry* forConsole(Console c) const;  // nullptr für N64/GC/Wii
    const EmulatorEntry* forN64(N64Method m) const;    // [n64_not64]/[n64_wii64]; nullptr für Vc
    const EmulatorEntry& nintendont() const { return nintendont_; }  // [nintendont] dol; args gilt nicht

private:
    std::array<std::optional<EmulatorEntry>, kConsoleCount> entries_{};
    EmulatorEntry not64_, wii64_, nintendont_;
};

std::string defaultEmulatorsText();  // Inhalt einer neu angelegten emulators.ini, exakt wie Spec 5.4

}  // namespace nl
