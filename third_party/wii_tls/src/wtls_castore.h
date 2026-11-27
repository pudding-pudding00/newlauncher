/* Trusted CA store: embedded Mozilla roots (lazy, parsed per lookup) plus
 * CAs added at runtime with wtls_add_trusted_ca. */
#ifndef WTLS_CASTORE_H
#define WTLS_CASTORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <mbedtls/x509_crt.h>

typedef struct {
    uint32_t subject_hash;  /* FNV-1a-32 over the subject DER (TLV) */
    uint16_t subj_off;      /* subject TLV offset inside der */
    uint16_t subj_len;
    uint32_t der_len;
    const uint8_t *der;
} wtls_ca_entry;

#ifndef WTLS_EMBED_CA
#define WTLS_EMBED_CA 1
#endif

#if WTLS_EMBED_CA
/* Generated from data/cacert.pem (tools/gen_ca_bundle.py), sorted by subject_hash. */
extern const wtls_ca_entry wtls_ca_bundle[];
extern const size_t wtls_ca_bundle_count;
#endif

uint32_t wtls_fnv1a32(const uint8_t *p, size_t n);

/* DER or PEM (PEM: len includes the terminating '\0'). WTLS_OK | WTLS_ERR_ARG. */
int wtls_castore_add(const uint8_t *buf, size_t len);

/* mbedtls_x509_crt_ca_cb_t: candidates whose subject equals child->issuer_raw,
 * as a heap list owned by the caller; *candidates = NULL and 0 if none. */
int wtls_castore_ca_cb(void *ctx, const mbedtls_x509_crt *child, mbedtls_x509_crt **candidates);

/* Drops the runtime-added CAs and sets whether the built-in bundle is used. */
void wtls_castore_reset(bool keep_builtin);

/* Drops the runtime-added CAs and restores default built-in usage (true). */
void wtls_castore_free(void);

/* Number of callback invocations (used to detect resumed handshakes). */
extern uint32_t wtls_castore_lookups;

#endif
