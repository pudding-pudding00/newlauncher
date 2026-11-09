#include "platform/log.hpp"

#include <cstdarg>
#include <cstdio>

#include <gccore.h>

namespace nl::log {

void init() {
    SYS_STDIO_Report(true);
}

void line(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    // libogc übersetzt '\n' in das '\r', bei dem Dolphin eine Logzeile abschließt.
    SYS_Report("%s\n", buf);
}

}  // namespace nl::log
