/* Small DNS cache in front of the (blocking) platform resolver. */
#ifndef WTLS_DNS_H
#define WTLS_DNS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "plat/plat.h"

#define WTLS_DNS_SLOTS 16
#define WTLS_DNS_TTL_MS 300000u

typedef int (*wtls_resolver_fn)(const char *host, uint32_t *ipv4_be);
typedef int (*wtls_resolver_all_fn)(const char *host, uint32_t *addrs, size_t cap, size_t *count);

/* IP literal directly, else cache (case-insensitive, TTL), else resolver.
 * Failures are not cached. OK | ERR_DNS | ERR_ARG. */
int wtls_dns_lookup(const char *host, uint32_t *ipv4_be, uint64_t now_ms);
/* Multi-IPv4 lookup for connect fallback: cache hit yields 1 IP (*from_cache = true);
 * cache miss yields up to WTLS_DNS_MAX_ADDRS without caching until wtls_dns_commit(). */
int wtls_dns_lookup_list(const char *host, uint32_t addrs[WTLS_DNS_MAX_ADDRS], size_t *count,
                         bool *from_cache, uint64_t now_ms);
void wtls_dns_commit(const char *host, uint32_t ipv4_be, uint64_t now_ms);
void wtls_dns_invalidate(const char *host);
/* Test hooks; NULL restores the default platform resolver. */
void wtls_dns_set_resolver(wtls_resolver_fn fn);
void wtls_dns_set_resolver_all(wtls_resolver_all_fn fn);
void wtls_dns_clear(void);

#endif
