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
static wtls_resolver_fn s_resolver;
static wtls_resolver_all_fn s_resolver_all;

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

static void store_entry(const char *key, uint32_t ip, uint64_t now_ms) {
    entry *e = find(key);
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
}

static int resolve_addrs(const char *key, uint32_t addrs[WTLS_DNS_MAX_ADDRS], size_t *count) {
    if (s_resolver_all) return s_resolver_all(key, addrs, WTLS_DNS_MAX_ADDRS, count);
    if (s_resolver) {
        if (s_resolver(key, &addrs[0]) != WTLS_OK) return WTLS_ERR_DNS;
        *count = 1;
        return WTLS_OK;
    }
    return wtls_plat_resolve_all(key, addrs, WTLS_DNS_MAX_ADDRS, count);
}

int wtls_dns_lookup_list(const char *host, uint32_t addrs[WTLS_DNS_MAX_ADDRS], size_t *count,
                         bool *from_cache, uint64_t now_ms) {
    char key[HOST_MAX];
    if (from_cache) *from_cache = false;
    if (count) *count = 0;
    if (!host || !addrs || !count || !lower_copy(host, key)) return WTLS_ERR_ARG;
    if (wtls_plat_parse_ipv4(key, &addrs[0])) {
        *count = 1;
        return WTLS_OK;
    }
    entry *e = find(key);
    if (e && now_ms - e->resolved_ms <= WTLS_DNS_TTL_MS) {
        e->used_ms = now_ms;
        addrs[0] = e->ip;
        *count = 1;
        if (from_cache) *from_cache = true;
        return WTLS_OK;
    }
    size_t n = 0;
    if (resolve_addrs(key, addrs, &n) != WTLS_OK || n == 0) {
        if (e) memset(e, 0, sizeof *e);
        return WTLS_ERR_DNS;
    }
    if (n > WTLS_DNS_MAX_ADDRS) n = WTLS_DNS_MAX_ADDRS;
    *count = n;
    return WTLS_OK;
}

void wtls_dns_commit(const char *host, uint32_t ipv4_be, uint64_t now_ms) {
    char key[HOST_MAX];
    if (!host || !lower_copy(host, key) || wtls_plat_parse_ipv4(key, NULL)) return;
    store_entry(key, ipv4_be, now_ms);
}

int wtls_dns_lookup(const char *host, uint32_t *ipv4_be, uint64_t now_ms) {
    if (!ipv4_be) return WTLS_ERR_ARG;
    uint32_t addrs[WTLS_DNS_MAX_ADDRS];
    size_t count = 0;
    bool from_cache = false;
    int r = wtls_dns_lookup_list(host, addrs, &count, &from_cache, now_ms);
    if (r != WTLS_OK) return r;
    *ipv4_be = addrs[0];
    if (!from_cache) wtls_dns_commit(host, addrs[0], now_ms);
    return WTLS_OK;
}

void wtls_dns_invalidate(const char *host) {
    char key[HOST_MAX];
    if (!host || !lower_copy(host, key)) return;
    entry *e = find(key);
    if (e) memset(e, 0, sizeof *e);
}

void wtls_dns_set_resolver(wtls_resolver_fn fn) {
    s_resolver = fn;
    if (fn) s_resolver_all = NULL;
}

void wtls_dns_set_resolver_all(wtls_resolver_all_fn fn) {
    s_resolver_all = fn;
    if (fn) s_resolver = NULL;
}

void wtls_dns_clear(void) { memset(s_cache, 0, sizeof s_cache); }
