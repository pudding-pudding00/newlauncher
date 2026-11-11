#include "wtls_time.h"

#include <string.h>

#include <psa/crypto.h>

#include "wtls_internal.h"

#ifndef WTLS_BUILD_UNIX_TIME
#error "WTLS_BUILD_UNIX_TIME must be defined by the build system"
#endif

#define YEAR_S (365LL * 86400)
#define NTP_PORT 123

int64_t wtls_time_build(void) { return (int64_t)WTLS_BUILD_UNIX_TIME; }

int wtls_time_check(int64_t now_s) {
    int64_t b = wtls_time_build();
    return (now_s >= b - 86400 && now_s < b + 20 * YEAR_S) ? WTLS_OK : WTLS_ERR_CLOCK;
}

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint64_t be64(const uint8_t *p) { return ((uint64_t)be32(p) << 32) | be32(p + 4); }

size_t wtls_sntp_build_request(uint8_t out[48], uint64_t nonce) {
    memset(out, 0, 48);
    out[0] = 0x23; /* LI 0, VN 4, mode 3 (client) */
    for (int i = 0; i < 8; i++) out[40 + i] = (uint8_t)(nonce >> (56 - 8 * i));
    return 48;
}

int wtls_sntp_parse(const uint8_t *pkt, size_t len, uint64_t nonce, int64_t *unix_s) {
    if (!pkt || len < 48 || !unix_s) return WTLS_ERR_IO;
    uint8_t li = pkt[0] >> 6, mode = pkt[0] & 7, stratum = pkt[1];
    if (mode != 4 || li == 3 || stratum < 1 || stratum > 15) return WTLS_ERR_IO;
    if (be64(pkt + 24) != nonce) return WTLS_ERR_IO;
    if (be64(pkt + 40) == 0) return WTLS_ERR_IO;
    int64_t secs = be32(pkt + 40);
    /* RFC 4330 section 3: MSB clear means NTP era 1 (2036-02-07 onwards). */
    if (secs < 0x80000000LL) secs += 0x100000000LL;
    *unix_s = secs - WTLS_NTP_UNIX_DELTA;
    return WTLS_OK;
}

mbedtls_time_t wtls_mbedtls_time(mbedtls_time_t *t) {
    mbedtls_time_t now = (mbedtls_time_t)wtls_plat_unix_time();
    if (t) *t = now;
    return now;
}

wtls_status wtls_time_sync_sntp(const char *server, uint32_t timeout_ms) {
    if (!server) server = "pool.ntp.org";
    if (timeout_ms == 0) return WTLS_ERR_ARG;
    if (!wtls_core_initialized()) return WTLS_ERR_STATE; /* PSA state is owned by wtls_init */
    uint64_t nonce;
    /* An unpredictable nonce stops off-path spoofed replies. */
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_generate_random((uint8_t *)&nonce, sizeof nonce) != PSA_SUCCESS)
        return WTLS_ERR_ENTROPY;
    uint32_t ip;
    if (wtls_plat_resolve(server, &ip) != WTLS_OK) return WTLS_ERR_DNS;
    uint8_t req[48], resp[68];
    wtls_sntp_build_request(req, nonce);
    int n = wtls_plat_udp_exchange(ip, NTP_PORT, req, sizeof req, resp, sizeof resp, timeout_ms);
    if (n < 0) return (wtls_status)n;
    int64_t unix_s;
    if (wtls_sntp_parse(resp, (size_t)n, nonce, &unix_s) != WTLS_OK) return WTLS_ERR_IO;
    if (wtls_time_check(unix_s) != WTLS_OK) return WTLS_ERR_CLOCK; /* server is wrong */
    wtls_plat_set_unix_time_offset(0);
    wtls_plat_set_unix_time_offset(unix_s - wtls_plat_unix_time());
    return WTLS_OK;
}
