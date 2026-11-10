#include "platform/log.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#include <gccore.h>

namespace nl::log {

namespace {
constexpr std::size_t kRingBytes = 16 * 1024;
char g_ring[kRingBytes];
std::size_t g_head = 0;  // nächste Schreibposition
bool g_wrapped = false;

void appendRing(const char* s) {
    for (; *s != '\0'; ++s) {
        g_ring[g_head++] = *s;
        if (g_head == kRingBytes) {
            g_head = 0;
            g_wrapped = true;
        }
    }
}
}  // namespace

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
    appendRing(buf);
    appendRing("\n");
}

bool flushLogFile(FileSystem& fs, std::string_view path) {
    // Älteste Bytes zuerst: bei Überlauf ab g_head bis zum Ende, dann vom Anfang bis g_head.
    const std::size_t tail = g_wrapped ? kRingBytes - g_head : 0;
    std::vector<u8> data(tail + g_head);
    if (tail > 0) std::memcpy(data.data(), g_ring + g_head, tail);
    if (g_head > 0) std::memcpy(data.data() + tail, g_ring, g_head);
    return fs.writeAtomic(path, data).isOk();  // Fehler beim Log-Schreiben sind nicht kritisch
}

}  // namespace nl::log
