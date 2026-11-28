/* Clock sanity check and SNTP (RFC 4330) time sync. */
#ifndef WTLS_TIME_H
#define WTLS_TIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <mbedtls/platform_time.h>

#include "plat/plat.h"

/* Seconds between 1900-01-01 (NTP epoch) and 1970-01-01 (Unix epoch). */
#define WTLS_NTP_UNIX_DELTA 2208988800LL

/* Unix time of the build (WTLS_BUILD_UNIX_TIME, from CMake). */
int64_t wtls_time_build(void);

/* WTLS_OK if build-86400 <= now_s < build + 20*365*86400, else WTLS_ERR_CLOCK. */
int wtls_time_check(int64_t now_s);

/* SNTPv4 client request (LI 0, VN 4, mode 3) with nonce in the transmit timestamp.
 * Returns 48. */
size_t wtls_sntp_build_request(uint8_t out[48], uint64_t nonce);

/* Accepts only: len >= 48, mode 4, LI != 3, stratum 1..15, originate == nonce,
 * transmit != 0. Returns WTLS_OK (unix_s set) or WTLS_ERR_IO. */
int wtls_sntp_parse(const uint8_t *pkt, size_t len, uint64_t nonce, int64_t *unix_s);

/* One UDP request/response (wtls_plat_udp_exchange's contract, plus a context). */
typedef int (*wtls_udp_exchange_fn)(void *ctx, uint32_t ipv4_be, uint16_t port, const uint8_t *req,
                                    size_t req_len, uint8_t *resp, size_t resp_cap,
                                    uint32_t timeout_ms);

/* SNTP query to ipv4_be:123 with up to 3 attempts of timeout_ms/3 (>= 1 ms)
 * each: a lost packet (WTLS_ERR_TIMEOUT) or a reply that fails
 * wtls_sntp_parse is retried, a socket error is not. Returns WTLS_OK (unix_s
 * set) or the last attempt's error. */
int wtls_sntp_query(wtls_udp_exchange_fn fn, void *ctx, uint32_t ipv4_be, uint64_t nonce,
                    uint32_t timeout_ms, int64_t *unix_s);

/* Persisted SNTP offset (wtls_time_save/load): magic "WTLSTM01", offset and raw RTC time
 * at save (both big-endian int64), CRC-32 of the first 24 bytes. */
#define WTLS_TIME_STATE_LEN 28u
void wtls_time_state_encode(uint8_t out[WTLS_TIME_STATE_LEN], int64_t offset, int64_t rtc_now);
/* WTLS_OK (*offset set); WTLS_ERR_FILE (bad magic or CRC); WTLS_ERR_CLOCK (the RTC went back
 * since the save, more than max_age_s passed (0 = no limit), or the corrected time is
 * implausible by wtls_time_check). */
int wtls_time_state_decode(const uint8_t in[WTLS_TIME_STATE_LEN], int64_t rtc_now, uint32_t max_age_s,
                           int64_t *offset);
/* Records whether the offset in use came from a real SNTP exchange (tests set it too). */
void wtls_time_note_synced(bool synced);

/* mbedtls time callback (registered with mbedtls_platform_set_time in wtls_init). */
mbedtls_time_t wtls_mbedtls_time(mbedtls_time_t *t);

#endif
