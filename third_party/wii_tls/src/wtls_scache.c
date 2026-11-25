#include "wtls_scache.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <wiitls/wiitls.h>

#define HOST_MAX 256

typedef struct {
    char host[HOST_MAX]; /* lower case; "" = free slot */
    uint16_t port;
    bool has_cred;
    uint8_t cred_fp[WTLS_SCACHE_FP_LEN]; /* valid when has_cred */
    uint64_t stored_ms;
    uint32_t seq;        /* insertion order for LRU eviction */
    size_t len;
    uint8_t blob[WTLS_SESSION_BLOB_MAX];
} slot;

static slot s_slots[WTLS_SCACHE_SLOTS];
static uint32_t s_seq;

static bool lower_copy(const char *in, char out[HOST_MAX]) {
    size_t n = strlen(in);
    if (n == 0 || n >= HOST_MAX) return false;
    for (size_t i = 0; i <= n; i++) {
        char ch = in[i];
        out[i] = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
    }
    return true;
}

static bool same_key(const slot *s, const char *key, uint16_t port, bool has_cred,
                     const uint8_t *cred_fp) {
    if (!s->host[0] || s->port != port || s->has_cred != has_cred || strcmp(s->host, key) != 0)
        return false;
    return !has_cred || memcmp(s->cred_fp, cred_fp, WTLS_SCACHE_FP_LEN) == 0;
}

static slot *find(const char *key, uint16_t port, bool has_cred, const uint8_t *cred_fp) {
    for (int i = 0; i < WTLS_SCACHE_SLOTS; i++)
        if (same_key(&s_slots[i], key, port, has_cred, cred_fp)) return &s_slots[i];
    return NULL;
}

int wtls_scache_put(const char *host, uint16_t port, bool has_cred, const uint8_t *cred_fp,
                    const uint8_t *blob, size_t len, uint64_t now_ms) {
    char key[HOST_MAX];
    if (!host || !blob || len == 0 || len > WTLS_SESSION_BLOB_MAX || !lower_copy(host, key))
        return WTLS_ERR_ARG;
    if (has_cred && !cred_fp) return WTLS_ERR_ARG;
    slot *s = find(key, port, has_cred, cred_fp);
    if (!s) {
        s = &s_slots[0];
        for (int i = 0; i < WTLS_SCACHE_SLOTS; i++) {
            if (!s_slots[i].host[0]) {
                s = &s_slots[i];
                break;
            }
            if (s_slots[i].seq < s->seq) s = &s_slots[i];
        }
    }
    memcpy(s->host, key, strlen(key) + 1);
    s->port = port;
    s->has_cred = has_cred;
    if (has_cred) memcpy(s->cred_fp, cred_fp, WTLS_SCACHE_FP_LEN);
    s->stored_ms = now_ms;
    s->seq = ++s_seq;
    s->len = len;
    memcpy(s->blob, blob, len);
    return WTLS_OK;
}

int wtls_scache_take(const char *host, uint16_t port, bool has_cred, const uint8_t *cred_fp,
                     uint8_t *blob, size_t cap, size_t *len, uint64_t now_ms) {
    char key[HOST_MAX];
    if (!host || !blob || !len || !lower_copy(host, key)) return WTLS_ERR_ARG;
    if (has_cred && !cred_fp) return WTLS_ERR_ARG;
    slot *s = find(key, port, has_cred, cred_fp);
    if (!s) return WTLS_ERR_STATE;
    bool ok = now_ms - s->stored_ms <= WTLS_SESSION_MAX_AGE_MS && s->len <= cap;
    if (ok) {
        memcpy(blob, s->blob, s->len);
        *len = s->len;
    }
    memset(s, 0, sizeof *s); /* single use, and wipe the secret */
    return ok ? WTLS_OK : WTLS_ERR_STATE;
}

void wtls_scache_clear(void) {
    memset(s_slots, 0, sizeof s_slots);
    s_seq = 0;
}
