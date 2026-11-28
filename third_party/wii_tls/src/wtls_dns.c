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
static uint32_t s_gen; /* bumped by wtls_dns_clear(): answers of older asynchronous lookups are dropped */
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

void wtls_dns_clear(void) {
    memset(s_cache, 0, sizeof s_cache);
    wtls_plat_async_lock();
    s_gen++; /* answers of lookups that are still running belong to the old network */
    wtls_plat_async_unlock();
}

/* ---- asynchronous lookups --------------------------------------------------- */

enum { JOB_FREE = 0, JOB_RUNNING, JOB_DONE, JOB_ABANDONED };

typedef struct {
    int state;
    uint32_t gen;
    char host[HOST_MAX];
    uint32_t addrs[WTLS_DNS_MAX_ADDRS];
    size_t count;
    int status;
} dns_job;

static dns_job s_jobs[WTLS_DNS_JOBS];

static void job_worker(void *arg) {
    dns_job *j = arg;
    char host[HOST_MAX];
    wtls_plat_async_lock();
    memcpy(host, j->host, sizeof host);
    wtls_plat_async_unlock();
    uint32_t addrs[WTLS_DNS_MAX_ADDRS];
    size_t n = 0;
    int st = resolve_addrs(host, addrs, &n); /* the slow part, outside the lock */
    wtls_plat_async_lock();
    if (j->state == JOB_ABANDONED) {
        j->state = JOB_FREE;
    } else {
        j->status = (st == WTLS_OK && n > 0) ? WTLS_OK : WTLS_ERR_DNS;
        j->count = n > WTLS_DNS_MAX_ADDRS ? WTLS_DNS_MAX_ADDRS : n;
        if (j->status == WTLS_OK) memcpy(j->addrs, addrs, j->count * sizeof addrs[0]);
        j->state = JOB_DONE;
    }
    wtls_plat_async_unlock();
}

int wtls_dns_lookup_begin(const char *host, int *job, uint32_t addrs[WTLS_DNS_MAX_ADDRS], size_t *count,
                          bool *from_cache, uint64_t now_ms) {
    char key[HOST_MAX];
    if (job) *job = -1;
    if (from_cache) *from_cache = false;
    if (count) *count = 0;
    if (!host || !job || !addrs || !count || !lower_copy(host, key)) return WTLS_ERR_ARG;
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
    wtls_plat_async_lock();
    /* A retry after giving up on this host (an error, a timeout) takes its lookup back rather than starting another:
     * otherwise every retry would hold one more slot until the resolver answers. */
    for (int i = 0; i < WTLS_DNS_JOBS; i++) {
        dns_job *j = &s_jobs[i];
        if (j->state == JOB_ABANDONED && j->gen == s_gen && strcmp(j->host, key) == 0) {
            j->state = JOB_RUNNING;
            wtls_plat_async_unlock();
            *job = i;
            return WTLS_WANT_READ;
        }
    }
    int slot = -1;
    for (int i = 0; i < WTLS_DNS_JOBS; i++)
        if (s_jobs[i].state == JOB_FREE) {
            slot = i;
            break;
        }
    if (slot >= 0) {
        dns_job *j = &s_jobs[slot];
        memset(j, 0, sizeof *j);
        j->state = JOB_RUNNING;
        j->gen = s_gen;
        memcpy(j->host, key, strlen(key) + 1);
    }
    wtls_plat_async_unlock();
    if (slot < 0) return WTLS_ERR_DNS; /* every slot held by a lookup still running, often abandoned ones */
    int rc = wtls_plat_async_run(job_worker, &s_jobs[slot]);
    if (rc == WTLS_OK) {
        *job = slot;
        return WTLS_WANT_READ;
    }
    /* No helper thread: resolve inline, exactly like the blocking lookup. */
    wtls_plat_async_lock();
    s_jobs[slot].state = JOB_FREE;
    wtls_plat_async_unlock();
    return wtls_dns_lookup_list(host, addrs, count, from_cache, now_ms);
}

int wtls_dns_job_poll(int job, uint32_t addrs[WTLS_DNS_MAX_ADDRS], size_t *count) {
    if (job < 0 || job >= WTLS_DNS_JOBS || !addrs || !count) return WTLS_ERR_ARG;
    int rc;
    wtls_plat_async_lock();
    dns_job *j = &s_jobs[job];
    if (j->state == JOB_RUNNING) {
        rc = WTLS_WANT_READ;
    } else if (j->state == JOB_DONE) {
        rc = j->status;
        if (rc == WTLS_OK && j->gen != s_gen) rc = WTLS_ERR_DNS; /* the network was reset meanwhile */
        if (rc == WTLS_OK) {
            *count = j->count;
            memcpy(addrs, j->addrs, j->count * sizeof addrs[0]);
        }
        j->state = JOB_FREE;
    } else {
        rc = WTLS_ERR_ARG;
    }
    wtls_plat_async_unlock();
    return rc;
}

void wtls_dns_job_cancel(int job) {
    if (job < 0 || job >= WTLS_DNS_JOBS) return;
    wtls_plat_async_lock();
    dns_job *j = &s_jobs[job];
    if (j->state == JOB_RUNNING) j->state = JOB_ABANDONED; /* the worker frees the slot when it is through */
    else if (j->state == JOB_DONE) j->state = JOB_FREE;
    wtls_plat_async_unlock();
}
