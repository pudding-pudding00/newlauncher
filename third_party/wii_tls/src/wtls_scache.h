/* TLS 1.3 session ticket cache: one serialized session per
 * host+port+client-identity, single use (RFC 8446 §C.4), LRU eviction, max
 * age 24 h. Single-threaded like the API.
 *
 * The key includes the client certificate fingerprint: a connection with a
 * client certificate must never resume (and never feed) a session of a
 * connection without one, or with a different one - resumption skips the
 * server's certificate check, so the identities must match exactly.
 * has_cred false means cred_fp is ignored. */
#ifndef WTLS_SCACHE_H
#define WTLS_SCACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WTLS_SCACHE_SLOTS 8
#define WTLS_SESSION_BLOB_MAX 4096
#define WTLS_SESSION_MAX_AGE_MS (24u * 3600u * 1000u)
#define WTLS_SCACHE_FP_LEN 32u

/* Replaces an entry with the same key. OK | ERR_ARG (too large, bad args). */
int wtls_scache_put(const char *host, uint16_t port, bool has_cred, const uint8_t *cred_fp,
                    const uint8_t *blob, size_t len, uint64_t now_ms);
/* OK (entry copied and removed) | ERR_STATE (miss, expired or cap too small). */
int wtls_scache_take(const char *host, uint16_t port, bool has_cred, const uint8_t *cred_fp,
                     uint8_t *blob, size_t cap, size_t *len, uint64_t now_ms);
void wtls_scache_clear(void);

#endif
