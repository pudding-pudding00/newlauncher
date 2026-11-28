#include "wtls_castore.h"

#include <string.h>

#include <mbedtls/platform.h>

#include "wtls_mem.h"
#include <wiitls/wiitls.h>

uint32_t wtls_castore_lookups;

/* Runtime-added CAs (owned copies, linked list head). */
static mbedtls_x509_crt *s_extra;
/* Bundle loaded from a file (see wtls_castore_set_file_bundle). */
static uint8_t *s_file_blob;
static wtls_ca_entry *s_file_entries;
static size_t s_file_count;
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

static void drop_file_bundle(void) {
    wtls_free(s_file_entries);
    wtls_free(s_file_blob);
    s_file_entries = NULL;
    s_file_blob = NULL;
    s_file_count = 0;
}

void wtls_castore_set_file_bundle(uint8_t *blob, wtls_ca_entry *entries, size_t count) {
    drop_file_bundle();
    s_file_blob = blob;
    s_file_entries = entries;
    s_file_count = count;
}

void wtls_castore_reset(bool keep_builtin) {
    drop_file_bundle();
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

/* Appends the certificates of a hash-sorted table whose subject equals issuer to *list
 * (allocated on first use). A certificate that does not parse is skipped.
 * 0 or MBEDTLS_ERR_X509_ALLOC_FAILED. */
static int table_candidates(mbedtls_x509_crt **list, const wtls_ca_entry *tab, size_t count, const uint8_t *issuer,
                            size_t ilen, bool nocopy) {
    uint32_t h = wtls_fnv1a32(issuer, ilen);
    size_t lo = 0, hi = count;
    while (lo < hi) { /* lower bound of h */
        size_t mid = lo + (hi - lo) / 2;
        if (tab[mid].subject_hash < h) lo = mid + 1;
        else hi = mid;
    }
    for (size_t i = lo; i < count && tab[i].subject_hash == h; i++) {
        const wtls_ca_entry *e = &tab[i];
        if (e->subj_len != ilen || memcmp(e->der + e->subj_off, issuer, ilen) != 0) continue;
        if (!*list) {
            *list = mbedtls_calloc(1, sizeof **list);
            if (!*list) return MBEDTLS_ERR_X509_ALLOC_FAILED;
            mbedtls_x509_crt_init(*list);
        }
        int ret = nocopy ? mbedtls_x509_crt_parse_der_nocopy(*list, e->der, e->der_len)
                         : mbedtls_x509_crt_parse_der(*list, e->der, e->der_len);
        if (ret == MBEDTLS_ERR_X509_ALLOC_FAILED) return ret;
    }
    return 0;
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
        ret = table_candidates(&list, wtls_ca_bundle, wtls_ca_bundle_count, issuer, ilen, true); /* .rodata outlives the list: no copy */
        if (ret != 0) goto fail;
    }
#endif
    if (s_file_count) {
        /* Copy: a reset may free the blob while the list is still alive. */
        ret = table_candidates(&list, s_file_entries, s_file_count, issuer, ilen, false);
        if (ret != 0) goto fail;
    }
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
