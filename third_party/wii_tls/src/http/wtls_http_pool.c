/* Keep-alive pool: WTLS_HTTP_POOL_SLOTS entries in put order (oldest first). */
#include "http/wtls_http_pool.h"
#include "wtls_internal.h"

#include <string.h>

typedef struct {
    void *c;
    bool secure;
    bool insecure;
    bool has_cred;
    bool h2;
    uint8_t cred_fp[WTLS_HTTP_POOL_FP_LEN]; /* valid when has_cred */
    uint16_t port;
    uint64_t put_ms;
    char host[256];
} slot;

static slot s_slots[WTLS_HTTP_POOL_SLOTS];
static size_t s_n;
static void (*s_closer)(void *);

void wtls_http_pool_set_closer(void (*fn)(void *)) { s_closer = fn; }

static void close_entry(void *c) {
    if (s_closer) s_closer(c);
    else wtls_http_tr_close(c);
}

static void remove_at(size_t i) {
    memmove(&s_slots[i], &s_slots[i + 1], (s_n - i - 1) * sizeof s_slots[0]);
    s_n--;
}

static bool same_key(const slot *s, bool secure, const char *host, uint16_t port, bool insecure,
                     bool has_cred, const uint8_t *cred_fp, bool h2) {
    if (s->secure != secure || s->insecure != insecure || s->has_cred != has_cred ||
        s->h2 != h2 || s->port != port || strcmp(s->host, host) != 0)
        return false;
    return !has_cred || memcmp(s->cred_fp, cred_fp, WTLS_HTTP_POOL_FP_LEN) == 0;
}

void wtls_http_pool_put(void *c, bool secure, const char *host, uint16_t port, bool insecure,
                        bool has_cred, const uint8_t *cred_fp, bool h2, uint64_t now_ms) {
    if (!c || !host) return;
    size_t hl = strlen(host);
    if (hl >= sizeof s_slots[0].host || (has_cred && !cred_fp) || (h2 && !secure)) {
        close_entry(c);
        return;
    }
    if (s_n == WTLS_HTTP_POOL_SLOTS) {
        void *oldest = s_slots[0].c;
        remove_at(0);
        close_entry(oldest);
    }
    slot *s = &s_slots[s_n++];
    s->c = c;
    s->secure = secure;
    s->insecure = insecure;
    s->has_cred = has_cred;
    s->h2 = h2;
    if (has_cred) memcpy(s->cred_fp, cred_fp, WTLS_HTTP_POOL_FP_LEN);
    s->port = port;
    s->put_ms = now_ms;
    memcpy(s->host, host, hl + 1);
}

void *wtls_http_pool_take(bool secure, const char *host, uint16_t port, bool insecure,
                          bool has_cred, const uint8_t *cred_fp, bool h2, uint64_t now_ms) {
    for (size_t i = 0; i < s_n;) {
        if (now_ms - s_slots[i].put_ms > WTLS_HTTP_POOL_IDLE_MS) {
            void *c = s_slots[i].c;
            remove_at(i);
            close_entry(c);
        } else {
            i++;
        }
    }
    if (!host || (has_cred && !cred_fp)) return NULL;
    for (size_t i = s_n; i-- > 0;) {
        slot *s = &s_slots[i];
        if (same_key(s, secure, host, port, insecure, has_cred, cred_fp, h2)) {
            void *c = s->c;
            remove_at(i);
            return c;
        }
    }
    return NULL;
}

void wtls_http_pool_flush(void) {
    if (!WTLS_THREAD_GUARD("wtls_http_pool_flush")) return;
    while (s_n) {
        void *c = s_slots[s_n - 1].c;
        s_n--;
        close_entry(c);
    }
}
