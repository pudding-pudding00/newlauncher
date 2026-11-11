#pragma once
// Systemzustand der Wii, den Startwege brauchen (Plan M8).

namespace nl::platform {

// true, wenn der HBC den Launcher mit Hardwarezugriff gestartet hat (<ahb_access/>): Nintendont braucht das.
bool ahbprotDisabled();

}  // namespace nl::platform
