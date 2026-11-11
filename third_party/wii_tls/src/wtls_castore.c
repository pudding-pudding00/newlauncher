#include "wtls_castore.h"

#include <string.h>

#include <mbedtls/platform.h>
#include <wiitls/wiitls.h>

uint32_t wtls_castore_lookups;

/* Runtime-added CAs (owned copies). */
static mbedtls_x509_crt s_extra;
static int s_extra_init;

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
    /* Validate into a scratch chain first so a partly bad PEM adds nothing. */
    mbedtls_x509_crt tmp;
    mbedtls_x509_crt_init(&tmp);
    int r = mbedtls_x509_crt_parse(&tmp, buf, len);
    mbedtls_x509_crt_free(&tmp);
    if (r != 0) return WTLS_ERR_ARG;
    if (!s_extra_init) {
        mbedtls_x509_crt_init(&s_extra);
        s_extra_init = 1;
    }
    r = mbedtls_x509_crt_parse(&s_extra, buf, len);
    return r == 0 ? WTLS_OK : (r == MBEDTLS_ERR_X509_ALLOC_FAILED ? WTLS_ERR_NOMEM : WTLS_ERR_ARG);
}

void wtls_castore_free(void) {
    if (s_extra_init) {
        mbedtls_x509_crt_free(&s_extra);
        s_extra_init = 0;
    }
}

static int same(const mbedtls_x509_buf *a, const uint8_t *p, size_t n) {
    return a->len == n && memcmp(a->p, p, n) == 0;
}

int wtls_castore_ca_cb(void *ctx, const mbedtls_x509_crt *child, mbedtls_x509_crt **candidates) {
    (void)ctx;
    wtls_castore_lookups++;
    *candidates = NULL;

    const uint8_t *issuer = child->issuer_raw.p;
    size_t ilen = child->issuer_raw.len;
    uint32_t h = wtls_fnv1a32(issuer, ilen);

    /* Lower bound of h in the hash-sorted bundle. */
    size_t lo = 0, hi = wtls_ca_bundle_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (wtls_ca_bundle[mid].subject_hash < h) lo = mid + 1;
        else hi = mid;
    }

    mbedtls_x509_crt *list = NULL;
    int ret = 0;
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
    if (s_extra_init) {
        for (const mbedtls_x509_crt *c = &s_extra; c && c->raw.p; c = c->next) {
            if (!same(&c->subject_raw, issuer, ilen)) continue;
            if (!list) {
                list = mbedtls_calloc(1, sizeof *list);
                if (!list) return MBEDTLS_ERR_X509_ALLOC_FAILED;
                mbedtls_x509_crt_init(list);
            }
            /* Copy: wtls_castore_free may run while the list is still alive. */
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
