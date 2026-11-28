#include "wtls_time.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <psa/crypto.h>

#include "wtls_internal.h"

#ifndef WTLS_BUILD_UNIX_TIME
#error "WTLS_BUILD_UNIX_TIME must be defined by the build system"
#endif

#define YEAR_S (365LL * 86400)

static bool s_synced; /* the offset in use came from SNTP in this run */
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

#define SNTP_ATTEMPTS 3

int wtls_sntp_query(wtls_udp_exchange_fn fn, void *ctx, uint32_t ipv4_be, uint64_t nonce,
                    uint32_t timeout_ms, int64_t *unix_s) {
    uint8_t req[48], resp[68];
    wtls_sntp_build_request(req, nonce);
    uint32_t each = timeout_ms / SNTP_ATTEMPTS;
    if (each == 0) each = 1;
    int r = WTLS_ERR_TIMEOUT;
    for (int attempt = 0; attempt < SNTP_ATTEMPTS; attempt++) {
        int n = fn(ctx, ipv4_be, NTP_PORT, req, sizeof req, resp, sizeof resp, each);
        if (n == WTLS_ERR_TIMEOUT) {
            r = n; /* lost request or reply: UDP over Wi-Fi drops packets */
            continue;
        }
        if (n < 0) return n;
        r = wtls_sntp_parse(resp, (size_t)n, nonce, unix_s);
        if (r == WTLS_OK) return WTLS_OK;
    }
    return r;
}

static int plat_exchange(void *ctx, uint32_t ipv4_be, uint16_t port, const uint8_t *req,
                         size_t req_len, uint8_t *resp, size_t resp_cap, uint32_t timeout_ms) {
    (void)ctx;
    return wtls_plat_udp_exchange(ipv4_be, port, req, req_len, resp, resp_cap, timeout_ms);
}

wtls_status wtls_time_sync_sntp(const char *server, uint32_t timeout_ms) {
    if (!server) server = "pool.ntp.org";
    if (timeout_ms == 0) return WTLS_ERR_ARG;
    if (!wtls_core_initialized()) return WTLS_ERR_STATE; /* PSA state is owned by wtls_init */
    if (!WTLS_THREAD_GUARD("wtls_time_sync_sntp")) return WTLS_ERR_STATE;
    uint64_t nonce;
    /* An unpredictable nonce stops off-path spoofed replies. */
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_generate_random((uint8_t *)&nonce, sizeof nonce) != PSA_SUCCESS)
        return WTLS_ERR_ENTROPY;
    uint32_t ip;
    if (wtls_plat_resolve(server, &ip) != WTLS_OK) return WTLS_ERR_DNS;
    int64_t unix_s;
    int r = wtls_sntp_query(plat_exchange, NULL, ip, nonce, timeout_ms, &unix_s);
    if (r != WTLS_OK) return (wtls_status)r;
    if (wtls_time_check(unix_s) != WTLS_OK) return WTLS_ERR_CLOCK; /* server is wrong */
    wtls_plat_set_unix_time_offset(0);
    wtls_plat_set_unix_time_offset(unix_s - wtls_plat_unix_time());
    s_synced = true;
    return WTLS_OK;
}

/* ---- persisted offset ----------------------------------------------------- */

static uint32_t crc32_of(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static void put_be64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (56 - 8 * i));
}

void wtls_time_note_synced(bool synced) { s_synced = synced; }

void wtls_time_state_encode(uint8_t out[WTLS_TIME_STATE_LEN], int64_t offset, int64_t rtc_now) {
    memcpy(out, "WTLSTM01", 8);
    put_be64(out + 8, (uint64_t)offset);
    put_be64(out + 16, (uint64_t)rtc_now);
    uint32_t crc = crc32_of(out, 24);
    for (int i = 0; i < 4; i++) out[24 + i] = (uint8_t)(crc >> (24 - 8 * i));
}

int wtls_time_state_decode(const uint8_t in[WTLS_TIME_STATE_LEN], int64_t rtc_now, uint32_t max_age_s,
                           int64_t *offset) {
    if (!in || !offset || memcmp(in, "WTLSTM01", 8) != 0) return WTLS_ERR_FILE;
    if (be32(in + 24) != crc32_of(in, 24)) return WTLS_ERR_FILE;
    int64_t off = (int64_t)be64(in + 8), saved_rtc = (int64_t)be64(in + 16);
    int64_t age = rtc_now - saved_rtc;
    if (age < 0 || (max_age_s && age > (int64_t)max_age_s)) return WTLS_ERR_CLOCK;
    if (wtls_time_check(rtc_now + off) != WTLS_OK) return WTLS_ERR_CLOCK;
    *offset = off;
    return WTLS_OK;
}

wtls_status wtls_time_save(const char *path) {
    if (!path) return WTLS_ERR_ARG;
    if (!s_synced) return WTLS_ERR_STATE; /* nothing trustworthy to remember */
    uint8_t rec[WTLS_TIME_STATE_LEN];
    wtls_time_state_encode(rec, wtls_plat_unix_time_offset(), (int64_t)time(NULL));
    FILE *f = fopen(path, "wb");
    if (!f) return WTLS_ERR_FILE;
    bool ok = fwrite(rec, 1, sizeof rec, f) == sizeof rec;
    ok = fclose(f) == 0 && ok;
    return ok ? WTLS_OK : WTLS_ERR_FILE;
}

wtls_status wtls_time_load(const char *path, uint32_t max_age_s) {
    if (!path) return WTLS_ERR_ARG;
    FILE *f = fopen(path, "rb");
    if (!f) return WTLS_ERR_FILE;
    uint8_t rec[WTLS_TIME_STATE_LEN + 1];
    size_t n = fread(rec, 1, sizeof rec, f);
    fclose(f);
    if (n != WTLS_TIME_STATE_LEN) return WTLS_ERR_FILE;
    int64_t off = 0;
    int r = wtls_time_state_decode(rec, (int64_t)time(NULL), max_age_s, &off);
    if (r != WTLS_OK) return (wtls_status)r;
    if (!s_synced) wtls_plat_set_unix_time_offset(off); /* a fresh SNTP result is better than the file */
    return WTLS_OK;
}
