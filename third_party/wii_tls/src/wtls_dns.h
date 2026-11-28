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
/* Asynchronous variant of wtls_dns_lookup_list(): a cache miss starts a resolver job on a helper thread.
 * WTLS_OK: addrs/count/from_cache are set and *job = -1 (IP literal, cache hit, or - on platforms without helper
 * threads - resolved right here). WTLS_WANT_READ: *job >= 0, ask wtls_dns_job_poll() later. WTLS_ERR_DNS (also when
 * all WTLS_DNS_JOBS slots are busy: a cancelled or timed-out lookup holds its slot until the resolver returns) or
 * WTLS_ERR_ARG. Like the blocking variant it does not cache: commit the working address with wtls_dns_commit(). */
#define WTLS_DNS_JOBS 4
int wtls_dns_lookup_begin(const char *host, int *job, uint32_t addrs[WTLS_DNS_MAX_ADDRS], size_t *count,
                          bool *from_cache, uint64_t now_ms);
/* WTLS_OK (job released, addrs/count set), WTLS_WANT_READ (still running), or WTLS_ERR_DNS (job released; also when the
 * network was reset (wtls_dns_clear) while the lookup ran: its answer is dropped, never cached). */
int wtls_dns_job_poll(int job, uint32_t addrs[WTLS_DNS_MAX_ADDRS], size_t *count);
/* Gives up on a job: its answer, when it arrives, is thrown away. */
void wtls_dns_job_cancel(int job);

void wtls_dns_commit(const char *host, uint32_t ipv4_be, uint64_t now_ms);
void wtls_dns_invalidate(const char *host);
/* Test hooks; NULL restores the default platform resolver. */
void wtls_dns_set_resolver(wtls_resolver_fn fn);
void wtls_dns_set_resolver_all(wtls_resolver_all_fn fn);
void wtls_dns_clear(void);

#endif
