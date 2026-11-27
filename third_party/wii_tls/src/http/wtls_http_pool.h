/* Keep-alive pool of idle HTTP connections (not installed).
 *
 * Pure apart from the closer: no I/O, no clock (time comes as now_ms).
 * Entries are opaque transport handles; the key is scheme, host, port,
 * insecure_skip_verify and the client certificate identity (fingerprint),
 * so an unverified connection never serves a verified request, and a
 * connection authenticated with one client certificate never serves a
 * request with another (or none). has_cred false means cred_fp is
 * ignored. */
#ifndef WTLS_HTTP_POOL_H
#define WTLS_HTTP_POOL_H

#include <stdbool.h>
#include <stdint.h>

#include <wiitls/http.h>

#define WTLS_HTTP_POOL_SLOTS 4
#define WTLS_HTTP_POOL_IDLE_MS 60000u
#define WTLS_HTTP_POOL_FP_LEN 32u

/* Adds c; a full pool closes its oldest entry first. */
void wtls_http_pool_put(void *c, bool secure, const char *host, uint16_t port, bool insecure,
                        bool has_cred, const uint8_t *cred_fp, bool h2, uint64_t now_ms);

/* Closes entries idle for more than WTLS_HTTP_POOL_IDLE_MS, then removes and
 * returns the most recently put match (LIFO). NULL = miss. */
void *wtls_http_pool_take(bool secure, const char *host, uint16_t port, bool insecure,
                          bool has_cred, const uint8_t *cred_fp, bool h2, uint64_t now_ms);

/* Test hook: how entries are closed. NULL => wtls_http_tr_close(). */
void wtls_http_pool_set_closer(void (*fn)(void *));

/* Closes a transport handle (src/http/wtls_http.c). */
void wtls_http_tr_close(void *c);

#endif
