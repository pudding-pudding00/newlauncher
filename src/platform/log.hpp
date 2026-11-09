#pragma once
// Log über den Debug-UART (SYS_Report): in Dolphin als OSReport sichtbar, auf echter Wii ein No-Op.

namespace nl::log {

void init();
void line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

}  // namespace nl::log
