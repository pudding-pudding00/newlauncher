#pragma once
// emulators.ini (Spec 5.4): welcher Emulator-DOL eine Konsole startet und mit welchem Argument-Schema.
// Fehlende Schlüssel kommen aus den Standardwerten; N64/GC/Wii laufen in M3 nicht über diese Tabelle.

#include <array>
#include <optional>
#include <string>

#include "core/console.hpp"
#include "core/ini.hpp"

namespace nl {

struct EmulatorEntry {
    std::string dol;
    std::string args;
};

class EmulatorTable {
public:
    static EmulatorTable fromIni(const IniDoc& doc);
    const EmulatorEntry* forConsole(Console c) const;  // nullptr für N64/GC/Wii

private:
    std::array<std::optional<EmulatorEntry>, kConsoleCount> entries_{};
};

std::string defaultEmulatorsText();  // Inhalt einer neu angelegten emulators.ini, exakt wie Spec 5.4

}  // namespace nl
