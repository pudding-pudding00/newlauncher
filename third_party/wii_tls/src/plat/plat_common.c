/* Platform-independent helpers of the platform layer. */
#include "plat.h"

#include <wiitls/wiitls.h>

#include <string.h>
#include <time.h>

/* Correction set by SNTP; the Wii RTC is often wrong. Single-thread model. */
static int64_t g_unix_offset;

int64_t wtls_plat_unix_time(void) { return (int64_t)time(NULL) + g_unix_offset; }

void wtls_plat_set_unix_time_offset(int64_t delta_s) { g_unix_offset = delta_s; }

bool wtls_plat_parse_ipv4(const char *s, uint32_t *ipv4_be) {
    uint8_t b[4];
    if (!s) return false;
    for (int part = 0; part < 4; part++) {
        unsigned v = 0, digits = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10 + (unsigned)(*s++ - '0');
            if (++digits > 3 || v > 255) return false;
        }
        if (digits == 0) return false;
        b[part] = (uint8_t)v;
        if (part < 3 && *s++ != '.') return false;
    }
    if (*s != '\0') return false;
    if (ipv4_be) memcpy(ipv4_be, b, 4); /* b[0] first in memory = network order */
    return true;
}

int wtls_plat_ios_io_result(int32_t r, bool is_send, int attempt) {
    if (r > 0) return (int)r;
    if (r == 0) return is_send ? WTLS_ERR_IO : 0;
    if (r == -WTLS_IOS_EAGAIN) return is_send ? WTLS_WANT_WRITE : WTLS_WANT_READ;
    if (r == WTLS_IPC_ENOMEM) {
        if (attempt < WTLS_IPC_RETRIES) return WTLS_PLAT_RETRY;
        return is_send ? WTLS_WANT_WRITE : WTLS_WANT_READ;
    }
    return WTLS_ERR_IO;
}

bool wtls_plat_ios_connect_pending(bool nonblocking, int32_t r) { return nonblocking && r < 0; }

bool wtls_plat_ios_connected(int32_t r) { return r == 0 || r == -WTLS_IOS_EISCONN; }
