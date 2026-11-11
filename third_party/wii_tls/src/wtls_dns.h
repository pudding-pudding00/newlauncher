/* Small DNS cache in front of the (blocking) platform resolver. */
#ifndef WTLS_DNS_H
#define WTLS_DNS_H

#include <stdint.h>

#define WTLS_DNS_SLOTS 16
#define WTLS_DNS_TTL_MS 300000u

typedef int (*wtls_resolver_fn)(const char *host, uint32_t *ipv4_be);

/* IP literal directly, else cache (case-insensitive, TTL), else resolver.
 * Failures are not cached. OK | ERR_DNS | ERR_ARG. */
int wtls_dns_lookup(const char *host, uint32_t *ipv4_be, uint64_t now_ms);
void wtls_dns_invalidate(const char *host);
/* Test hook; NULL restores the default wtls_plat_resolve. */
void wtls_dns_set_resolver(wtls_resolver_fn fn);
void wtls_dns_clear(void);

#endif
