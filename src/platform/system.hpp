#pragma once
// Systemzustand der Wii, den Startwege brauchen (Plan M8), Zufall und Wii-Jugendschutz (M17).

#include <optional>
#include <span>

#include "core/parental.hpp"

namespace nl::platform {

// Salz für die PIN (höchstens 32 Byte): SHA-256 über 64 Zeitstempel einer Warteschleife. Kein Krypto-Zufall,
// aber pro Wii und Zeitpunkt verschieden; die Kindersicherung ist ein Kinderschloss, kein Tresor.
void randomBytes(std::span<u8> out);

// IPL.PC aus /shared2/sys/SYSCONF, nur lesend (der Launcher schreibt nie in SYSCONF). nullopt, wenn die Datei
// fehlt, kaputt ist oder keine gültige PIN trägt.
std::optional<WiiParental> readWiiParental();

// true, wenn der HBC den Launcher mit Hardwarezugriff gestartet hat (<ahb_access/>): Nintendont braucht das.
bool ahbprotDisabled();

}  // namespace nl::platform
