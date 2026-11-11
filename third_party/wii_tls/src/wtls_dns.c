#include "wtls_dns.h"

#include <stdbool.h>
#include <string.h>

#include <wiitls/wiitls.h>

#include "plat/plat.h"

#define HOST_MAX 256

typedef struct {
    char host[HOST_MAX]; /* lower case; "" = free */
    uint32_t ip;
    uint64_t resolved_ms;
    uint64_t used_ms;    /* LRU */
} entry;

static entry s_cache[WTLS_DNS_SLOTS];
static wtls_resolver_fn s_resolver = wtls_plat_resolve;

static bool lower_copy(const char *in, char out[HOST_MAX]) {
    size_t n = strlen(in);
    if (n == 0 || n >= HOST_MAX) return false;
    for (size_t i = 0; i <= n; i++) {
        char ch = in[i];
        out[i] = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
    }
    return true;
}

static entry *find(const char *key) {
    for (int i = 0; i < WTLS_DNS_SLOTS; i++)
        if (s_cache[i].host[0] && strcmp(s_cache[i].host, key) == 0) return &s_cache[i];
    return NULL;
}

int wtls_dns_lookup(const char *host, uint32_t *ipv4_be, uint64_t now_ms) {
    char key[HOST_MAX];
    if (!host || !ipv4_be || !lower_copy(host, key)) return WTLS_ERR_ARG;
    if (wtls_plat_parse_ipv4(key, ipv4_be)) return WTLS_OK;
    entry *e = find(key);
    if (e && now_ms - e->resolved_ms <= WTLS_DNS_TTL_MS) {
        e->used_ms = now_ms;
        *ipv4_be = e->ip;
        return WTLS_OK;
    }
    uint32_t ip;
    if (s_resolver(key, &ip) != WTLS_OK) {
        if (e) memset(e, 0, sizeof *e);
        return WTLS_ERR_DNS;
    }
    if (!e) {
        e = &s_cache[0];
        for (int i = 0; i < WTLS_DNS_SLOTS; i++) {
            if (!s_cache[i].host[0]) {
                e = &s_cache[i];
                break;
            }
            if (s_cache[i].used_ms < e->used_ms) e = &s_cache[i];
        }
        memcpy(e->host, key, strlen(key) + 1);
    }
    e->ip = ip;
    e->resolved_ms = e->used_ms = now_ms;
    *ipv4_be = ip;
    return WTLS_OK;
}

void wtls_dns_invalidate(const char *host) {
    char key[HOST_MAX];
    if (!host || !lower_copy(host, key)) return;
    entry *e = find(key);
    if (e) memset(e, 0, sizeof *e);
}

void wtls_dns_set_resolver(wtls_resolver_fn fn) { s_resolver = fn ? fn : wtls_plat_resolve; }

void wtls_dns_clear(void) { memset(s_cache, 0, sizeof s_cache); }
