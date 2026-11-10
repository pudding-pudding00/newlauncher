#pragma once
// Log über den Debug-UART (SYS_Report): in Dolphin als OSReport sichtbar, auf echter Wii ein No-Op.

#include <string_view>

#include "core/fs.hpp"

namespace nl::log {

void init();
void line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
// Schreibt den Ringpuffer (letzte 16 KB) in eine Datei, z. B. "sd:/newlauncher/log.txt" (Spec 4.2).
// Nicht aus dem Absturz-Handler aufrufen.
bool flushLogFile(FileSystem& fs, std::string_view path);  // true, wenn geschrieben

}  // namespace nl::log
