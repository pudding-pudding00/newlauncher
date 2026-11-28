/* Platform-independent helpers of the platform layer. */
#include "plat.h"

#include <wiitls/wiitls.h>

#include <string.h>
#include <time.h>

/* Correction set by SNTP; the Wii RTC is often wrong. Single-thread model. */
static int64_t g_unix_offset;

int64_t wtls_plat_unix_time(void) { return (int64_t)time(NULL) + g_unix_offset; }

void wtls_plat_set_unix_time_offset(int64_t delta_s) { g_unix_offset = delta_s; }

int64_t wtls_plat_unix_time_offset(void) { return g_unix_offset; }

int wtls_plat_poll_many(const int *fds, const uint8_t *want, size_t n, uint32_t timeout_ms, uint8_t *ready) {
    uint64_t end = wtls_plat_now_ms() + timeout_ms;
    for (;;) {
        int count = 0;
        for (size_t i = 0; i < n; i++) {
            ready[i] = 0;
            if (fds[i] < 0) continue;
            if (wtls_plat_poll(fds[i], (want[i] & 1u) != 0, (want[i] & 2u) != 0, 0) != 0) { /* ready, or failed */
                ready[i] = 1;
                count++;
            }
        }
        if (count) return count;
        uint64_t now = wtls_plat_now_ms();
        if (now >= end) return 0;
        uint64_t left = end - now;
        wtls_plat_sleep_ms(left < 2 ? (uint32_t)left : 2u);
    }
}

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

/* Only EALREADY: after a refused attempt (RST) the socket is closed again and a
 * second connect starts a new attempt, answering EINPROGRESS (measured on a
 * Wii: treating that as "running" turned a refused connect into a timeout). */
bool wtls_plat_ios_connect_in_progress(int32_t r) { return r == -WTLS_IOS_EALREADY; }
