/* Loads a CA bundle (concatenated PEM certificates, e.g. cacert.pem) from a
 * file at run time. The file is streamed: only the DER blob that stays resident
 * (about 150 KiB for the Mozilla list) plus small buffers are in memory. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mbedtls/base64.h>
#include <mbedtls/x509_crt.h>
#include <wiitls/sig.h>

#include "wtls_castore.h"
#include "wtls_internal.h"
#include "wtls_mem.h"
#include "wtls_scache.h"

#define LINE_MAX_LEN 128
#define B64_MAX 16384u
#define DER_MAX 12288u
#define SIG_MAX 128u

typedef struct {
    uint32_t hash;
    uint16_t subj_off, subj_len;
    uint32_t der_off, der_len;
} tmp_entry;

typedef struct {
    uint8_t *blob;
    size_t blob_len, blob_cap;
    tmp_entry *ents;
    size_t n, cap;
    size_t skipped;
    /* line parser */
    char line[LINE_MAX_LEN];
    size_t line_len;
    bool overlong, in_cert, b64_bad;
    char *b64;
    size_t b64_len;
    uint8_t *der;
    wtls_status err;
} builder;

static void builder_free(builder *b) {
    wtls_free(b->blob);
    wtls_free(b->ents);
    wtls_free(b->b64);
    wtls_free(b->der);
}

static bool add_der(builder *b, const uint8_t *der, size_t len) {
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    bool ok = mbedtls_x509_crt_parse_der_nocopy(&crt, der, len) == 0 && crt.raw.p == der && crt.next == NULL;
    size_t off = 0, slen = 0;
    if (ok) {
        off = (size_t)(crt.subject_raw.p - der);
        slen = crt.subject_raw.len;
        ok = off + slen <= len && off <= UINT16_MAX && slen <= UINT16_MAX && slen > 0;
    }
    uint32_t hash = ok ? wtls_fnv1a32(der + off, slen) : 0;
    mbedtls_x509_crt_free(&crt);
    if (!ok) return false;

    if (b->blob_len + len > b->blob_cap) {
        size_t ncap = b->blob_cap ? b->blob_cap * 2 : 65536u;
        while (ncap < b->blob_len + len) ncap *= 2;
        uint8_t *nb = wtls_realloc(b->blob, ncap);
        if (!nb) {
            b->err = WTLS_ERR_NOMEM;
            return false;
        }
        b->blob = nb;
        b->blob_cap = ncap;
    }
    if (b->n == b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 256u;
        tmp_entry *ne = wtls_realloc(b->ents, ncap * sizeof *ne);
        if (!ne) {
            b->err = WTLS_ERR_NOMEM;
            return false;
        }
        b->ents = ne;
        b->cap = ncap;
    }
    memcpy(b->blob + b->blob_len, der, len);
    b->ents[b->n++] = (tmp_entry){hash, (uint16_t)off, (uint16_t)slen, (uint32_t)b->blob_len, (uint32_t)len};
    b->blob_len += len;
    return true;
}

static void finish_cert(builder *b) {
    size_t dl = 0;
    if (b->b64_bad || b->b64_len == 0 ||
        mbedtls_base64_decode(b->der, DER_MAX, &dl, (const unsigned char *)b->b64, b->b64_len) != 0 || dl == 0 ||
        !add_der(b, b->der, dl)) {
        if (b->err == WTLS_OK) b->skipped++;
    }
}

static void feed_line(builder *b) {
    char *l = b->line;
    size_t n = b->line_len;
    while (n && (l[n - 1] == '\r' || l[n - 1] == ' ' || l[n - 1] == '\t')) n--;
    l[n] = '\0';
    if (!b->in_cert) {
        if (!b->overlong && strcmp(l, "-----BEGIN CERTIFICATE-----") == 0) {
            b->in_cert = true;
            b->b64_len = 0;
            b->b64_bad = false;
        }
        return;
    }
    if (!b->overlong && strcmp(l, "-----END CERTIFICATE-----") == 0) {
        b->in_cert = false;
        finish_cert(b);
        return;
    }
    if (b->overlong || strncmp(l, "-----", 5) == 0 || n > B64_MAX - b->b64_len) { /* a stray marker or too much data */
        b->b64_bad = true;
        if (!b->overlong && strcmp(l, "-----BEGIN CERTIFICATE-----") == 0) { /* END missing: restart */
            b->skipped++;
            b->b64_len = 0;
            b->b64_bad = false;
        }
        return;
    }
    memcpy(b->b64 + b->b64_len, l, n);
    b->b64_len += n;
}

static void feed(builder *b, const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n && b->err == WTLS_OK; i++) {
        char c = (char)p[i];
        if (c == '\n') {
            feed_line(b);
            b->line_len = 0;
            b->overlong = false;
        } else if (b->line_len + 1 < LINE_MAX_LEN) {
            b->line[b->line_len++] = c;
        } else {
            b->overlong = true;
        }
    }
}

static int by_hash(const void *a, const void *b) {
    uint32_t x = ((const tmp_entry *)a)->hash, y = ((const tmp_entry *)b)->hash;
    return x < y ? -1 : x > y ? 1 : 0;
}

/* Reads path; optionally hashes its bytes. On WTLS_OK b holds the certificates. */
static wtls_status read_bundle(const char *path, builder *b, wtls_sha256_ctx *hash) {
    FILE *f = fopen(path, "rb");
    if (!f) return WTLS_ERR_FILE;
    b->b64 = wtls_malloc(B64_MAX);
    b->der = wtls_malloc(DER_MAX);
    if (!b->b64 || !b->der) {
        fclose(f);
        return WTLS_ERR_NOMEM;
    }
    uint8_t chunk[1024];
    size_t got;
    while ((got = fread(chunk, 1, sizeof chunk, f)) > 0) {
        if (hash && wtls_sha256_update(hash, chunk, got) != WTLS_OK) {
            fclose(f);
            return WTLS_ERR_INTERNAL;
        }
        feed(b, chunk, got);
        if (b->err != WTLS_OK) break;
    }
    bool io_err = ferror(f) != 0;
    fclose(f);
    if (b->err != WTLS_OK) return b->err;
    if (io_err) return WTLS_ERR_FILE;
    if (b->line_len || b->overlong) feed_line(b); /* last line without a newline */
    return b->err;
}

static wtls_status commit(builder *b, bool replace_builtin, size_t *n_loaded) {
    wtls_ca_entry *tab = wtls_malloc(b->n * sizeof *tab);
    if (!tab) return WTLS_ERR_NOMEM;
    qsort(b->ents, b->n, sizeof *b->ents, by_hash);
    for (size_t i = 0; i < b->n; i++) {
        const tmp_entry *e = &b->ents[i];
        tab[i] = (wtls_ca_entry){e->hash, e->subj_off, e->subj_len, e->der_len, b->blob + e->der_off};
    }
    if (replace_builtin) wtls_castore_reset(false); /* also drops CAs added at run time, like wtls_reset_trusted_cas() */
    wtls_castore_set_file_bundle(b->blob, tab, b->n);
    if (n_loaded) *n_loaded = b->n;
    b->blob = NULL; /* owned by the store now */
    wtls_scache_clear();
    if (wtls_pool_flush_hook) wtls_pool_flush_hook();
    return WTLS_OK;
}

static wtls_status load(const char *path, const char *sig_path, const uint8_t *pubkey, size_t pubkey_len,
                        bool replace_builtin, size_t *n_loaded) {
    if (n_loaded) *n_loaded = 0;
    if (!path || (sig_path && !pubkey)) return WTLS_ERR_ARG;
    if (!wtls_core_initialized()) return WTLS_ERR_STATE;
    if (!WTLS_THREAD_GUARD("wtls_load_ca_bundle_file")) return WTLS_ERR_STATE;

    uint8_t sig[SIG_MAX];
    size_t sig_len = 0;
    if (sig_path) { /* read it first: a missing signature must not cost a full pass */
        FILE *sf = fopen(sig_path, "rb");
        if (!sf) return WTLS_ERR_FILE;
        sig_len = fread(sig, 1, sizeof sig, sf);
        bool bad = ferror(sf) != 0 || !feof(sf) || sig_len == 0;
        fclose(sf);
        if (bad) return WTLS_ERR_FILE;
    }
    builder b;
    memset(&b, 0, sizeof b);
    wtls_sha256_ctx hash;
    wtls_status st = WTLS_OK;
    if (sig_path && (st = wtls_sha256_init(&hash)) != WTLS_OK) return st;
    st = read_bundle(path, &b, sig_path ? &hash : NULL);
    if (sig_path) {
        uint8_t digest[32];
        if (st == WTLS_OK) {
            st = wtls_sha256_finish(&hash, digest);
            if (st == WTLS_OK)
                st = wtls_sig_verify_p256_digest(pubkey, pubkey_len, digest, sig, sig_len, WTLS_SIG_DER);
        } else {
            wtls_sha256_abort(&hash);
        }
    }
    if (st == WTLS_OK && b.n == 0) st = WTLS_ERR_ARG; /* no usable certificate */
    if (st == WTLS_OK) st = commit(&b, replace_builtin, n_loaded);
    builder_free(&b);
    return st;
}

wtls_status wtls_load_ca_bundle_file(const char *path, bool replace_builtin, size_t *n_loaded) {
    return load(path, NULL, NULL, 0, replace_builtin, n_loaded);
}

wtls_status wtls_load_ca_bundle_file_signed(const char *path, const char *sig_path, const uint8_t *pubkey,
                                            size_t pubkey_len, bool replace_builtin, size_t *n_loaded) {
    if (!sig_path) {
        if (n_loaded) *n_loaded = 0;
        return WTLS_ERR_ARG;
    }
    return load(path, sig_path, pubkey, pubkey_len, replace_builtin, n_loaded);
}
