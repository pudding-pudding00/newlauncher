#include "wtls_castore.h"

#include <string.h>

#include <mbedtls/platform.h>
#include <wiitls/wiitls.h>

uint32_t wtls_castore_lookups;

/* Runtime-added CAs (owned copies, linked list head). */
static mbedtls_x509_crt *s_extra;
static bool s_use_builtin = true;

uint32_t wtls_fnv1a32(const uint8_t *p, size_t n) {
    uint32_t h = 0x811C9DC5u;
    while (n--) {
        h ^= *p++;
        h *= 0x01000193u;
    }
    return h;
}

int wtls_castore_add(const uint8_t *buf, size_t len) {
    if (!buf || len == 0) return WTLS_ERR_ARG;
    /* Parse once into a scratch chain so a partly bad PEM adds nothing. */
    mbedtls_x509_crt *tmp = mbedtls_calloc(1, sizeof *tmp);
    if (!tmp) return WTLS_ERR_NOMEM;
    mbedtls_x509_crt_init(tmp);
    int r = mbedtls_x509_crt_parse(tmp, buf, len);
    if (r != 0 || !tmp->raw.p) {
        mbedtls_x509_crt_free(tmp);
        mbedtls_free(tmp);
        return r == MBEDTLS_ERR_X509_ALLOC_FAILED ? WTLS_ERR_NOMEM : WTLS_ERR_ARG;
    }
    mbedtls_x509_crt **tail = &s_extra;
    while (*tail) tail = &(*tail)->next;
    *tail = tmp;
    return WTLS_OK;
}

void wtls_castore_reset(bool keep_builtin) {
    if (s_extra) {
        mbedtls_x509_crt_free(s_extra);
        mbedtls_free(s_extra);
        s_extra = NULL;
    }
    s_use_builtin = keep_builtin;
}

void wtls_castore_free(void) { wtls_castore_reset(true); }

static int same(const mbedtls_x509_buf *a, const uint8_t *p, size_t n) {
    return a->len == n && memcmp(a->p, p, n) == 0;
}

int wtls_castore_ca_cb(void *ctx, const mbedtls_x509_crt *child, mbedtls_x509_crt **candidates) {
    (void)ctx;
    wtls_castore_lookups++;
    *candidates = NULL;

    const uint8_t *issuer = child->issuer_raw.p;
    size_t ilen = child->issuer_raw.len;

    mbedtls_x509_crt *list = NULL;
    int ret = 0;
#if WTLS_EMBED_CA
    if (s_use_builtin) {
        uint32_t h = wtls_fnv1a32(issuer, ilen);

        /* Lower bound of h in the hash-sorted bundle. */
        size_t lo = 0, hi = wtls_ca_bundle_count;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (wtls_ca_bundle[mid].subject_hash < h) lo = mid + 1;
            else hi = mid;
        }

        for (size_t i = lo; i < wtls_ca_bundle_count && wtls_ca_bundle[i].subject_hash == h; i++) {
            const wtls_ca_entry *e = &wtls_ca_bundle[i];
            if (e->subj_len != ilen || memcmp(e->der + e->subj_off, issuer, ilen) != 0) continue;
            if (!list) {
                list = mbedtls_calloc(1, sizeof *list);
                if (!list) return MBEDTLS_ERR_X509_ALLOC_FAILED;
                mbedtls_x509_crt_init(list);
            }
            /* .rodata outlives the list: no copy. A root that does not parse is skipped. */
            ret = mbedtls_x509_crt_parse_der_nocopy(list, e->der, e->der_len);
            if (ret == MBEDTLS_ERR_X509_ALLOC_FAILED) goto fail;
            ret = 0;
        }
    }
#endif
    if (s_extra) {
        for (const mbedtls_x509_crt *c = s_extra; c && c->raw.p; c = c->next) {
            if (!same(&c->subject_raw, issuer, ilen)) continue;
            if (!list) {
                list = mbedtls_calloc(1, sizeof *list);
                if (!list) return MBEDTLS_ERR_X509_ALLOC_FAILED;
                mbedtls_x509_crt_init(list);
            }
            /* Copy: wtls_castore_free/reset may run while the list is still alive. */
            ret = mbedtls_x509_crt_parse_der(list, c->raw.p, c->raw.len);
            if (ret != 0) goto fail;
        }
    }
    if (list && !list->raw.p) { /* every match failed to parse */
        mbedtls_free(list);
        list = NULL;
    }
    *candidates = list;
    return 0;

fail:
    mbedtls_x509_crt_free(list);
    mbedtls_free(list);
    return ret;
}
