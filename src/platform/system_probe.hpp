#pragma once
// Konsolen-Merkmale für die Systeminfo (M13 Task 4, Spec v2 6.1). Nur lesen, nichts schreiben.

#include "core/types.hpp"

namespace nl::platform {

u16 sysMenuVersion();  // Titelversion des System-Menüs (TMD 00000001-00000002); 0 = nicht lesbar
bool latteDetected();  // vWii-Chip (Latte); nur mit AHB-Zugriff prüfbar, sonst false
u32 runningIos();      // IOS, unter dem der Launcher läuft

}  // namespace nl::platform
