/* Client credentials for mutual TLS (mTLS): loading and parsing. This is
 * the only object file with the cert/key parsing code - a TLS-only
 * application that never calls wtls_client_cred_load links none of it
 * (pinned by tools/check_no_symbol.cmake). The reference counting that the
 * connection code needs is inline in wtls_cred.h. */
#include <string.h>

#include <mbedtls/error.h>
#include <mbedtls/platform.h>
#include <mbedtls/private_access.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <psa/crypto.h>

#include "wtls_cred.h"
#include "wtls_internal.h"

/* Parses cert (PEM chain or DER, leaf first) and the private key, checks
 * that the key matches the leaf and fingerprints the leaf DER (SHA-256). */
wtls_status wtls_client_cred_load(wtls_client_cred **out,
                                  const uint8_t *cert, size_t cert_len,
                                  const uint8_t *key, size_t key_len) {
    if (!out) return WTLS_ERR_ARG;
    *out = NULL;
    if (!cert || cert_len == 0 || !key || key_len == 0) return WTLS_ERR_ARG;
    if (!wtls_core_initialized()) return WTLS_ERR_STATE;

    wtls_client_cred *c = mbedtls_calloc(1, sizeof *c);
    if (!c) return WTLS_ERR_NOMEM;
    mbedtls_x509_crt_init(&c->chain);
    mbedtls_pk_init(&c->key);

    /* A PEM blob needs its terminating NUL byte inside len (as documented
     * and as mbedtls_x509_crt_parse requires). */
    int r = mbedtls_x509_crt_parse(&c->chain, cert, cert_len);
    if (r != 0) goto fail_arg;
    r = mbedtls_pk_parse_key(&c->key, key, key_len, NULL, 0);
    if (r != 0) goto fail_arg;
    /* The key must match the leaf certificate. mbedtls_pk_check_pair is
     * unusable for this in 4.1.1: an RSA private key parsed by
     * mbedtls_pk_parse_key carries no pub_raw, so the check always fails
     * (PSA_ERROR_INVALID_ARGUMENT). Export the key's public part through
     * PSA instead and compare it with the leaf's parsed public key. */
    {
        uint8_t kpub[MBEDTLS_PK_MAX_PUBKEY_RAW_LEN];
        size_t kpub_len = 0;
        const mbedtls_pk_context *leaf_pk = &c->chain.pk;
        if (psa_export_public_key(c->key.MBEDTLS_PRIVATE(priv_id), kpub, sizeof kpub,
                                  &kpub_len) != PSA_SUCCESS ||
            kpub_len != leaf_pk->MBEDTLS_PRIVATE(pub_raw_len) ||
            memcmp(kpub, leaf_pk->MBEDTLS_PRIVATE(pub_raw), kpub_len) != 0) {
            goto fail_arg;
        }
    }

    size_t n = 0;
    if (psa_hash_compute(PSA_ALG_SHA_256, c->chain.raw.p, c->chain.raw.len, c->fp, sizeof c->fp,
                         &n) != PSA_SUCCESS || n != sizeof c->fp) {
        goto fail_arg;
    }

    c->refs = 1;
    *out = c;
    return WTLS_OK;

fail_arg:
    mbedtls_pk_free(&c->key);
    mbedtls_x509_crt_free(&c->chain);
    mbedtls_platform_zeroize(c, sizeof *c);
    mbedtls_free(c);
    return WTLS_ERR_ARG;
}

void wtls_client_cred_free(wtls_client_cred *cred) {
    wtls_client_cred_release(cred);
}
