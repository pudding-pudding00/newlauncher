#pragma once
// emulators.ini (Spec 5.4): welcher Emulator-DOL eine Konsole startet und mit welchem Argument-Schema.
// Fehlende Schlüssel kommen aus den Standardwerten; N64 über forN64, GC über nintendont(), Wii nicht hier.

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "core/console.hpp"
#include "core/ini.hpp"
#include "core/n64.hpp"

namespace nl {

struct EmulatorEntry {
    std::string dol;
    std::string args;
    bool fromFile = false;  // dol stand in emulators.ini (sonst v1-Vorgabe)
    std::string profile;    // nach resolveTable: erkanntes Profil; sonst leer
};

class EmulatorTable {
public:
    static EmulatorTable fromIni(const IniDoc& doc);
    const EmulatorEntry* forConsole(Console c) const;  // nullptr für N64/GC/Wii
    const EmulatorEntry* forN64(N64Method m) const;    // [n64_not64]/[n64_wii64]; nullptr für Vc
    const EmulatorEntry& nintendont() const { return nintendont_; }  // [nintendont] dol; args gilt nicht
    // Nach Abschnittsname: nes, snes, gb, gbc, gba, n64_not64, n64_wii64, nintendont; sonst nullptr / ignoriert.
    const EmulatorEntry* section(std::string_view name) const;
    void set(std::string_view name, EmulatorEntry e);

private:
    std::array<std::optional<EmulatorEntry>, kConsoleCount> entries_{};
    EmulatorEntry not64_, wii64_, nintendont_;
};

// Inhalt einer neu angelegten emulators.ini: nur ein Kommentarkopf (Spec v2 6.3: die Erkennung füllt die Lücken).
std::string defaultEmulatorsText();
std::string_view v1DefaultDol(std::string_view section);  // v1-Standardpfad (Spec 5.4); leer bei unbekanntem Abschnitt

}  // namespace nl
