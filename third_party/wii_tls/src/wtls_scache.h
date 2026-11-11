/* TLS 1.3 session ticket cache: one serialized session per host+port, single use
 * (RFC 8446 §C.4), LRU eviction, max age 24 h. Single-threaded like the API. */
#ifndef WTLS_SCACHE_H
#define WTLS_SCACHE_H

#include <stddef.h>
#include <stdint.h>

#define WTLS_SCACHE_SLOTS 8
#define WTLS_SESSION_BLOB_MAX 4096
#define WTLS_SESSION_MAX_AGE_MS (24u * 3600u * 1000u)

/* Replaces an entry with the same key. OK | ERR_ARG (too large, bad args). */
int wtls_scache_put(const char *host, uint16_t port, const uint8_t *blob, size_t len, uint64_t now_ms);
/* OK (entry copied and removed) | ERR_STATE (miss, expired or cap too small). */
int wtls_scache_take(const char *host, uint16_t port, uint8_t *blob, size_t cap, size_t *len, uint64_t now_ms);
void wtls_scache_clear(void);

#endif
