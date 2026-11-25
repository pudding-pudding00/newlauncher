/* Client credentials for mutual TLS (mTLS): internal shape of the opaque
 * wtls_client_cred. wtls_cred.c holds the loading/parsing (its object file
 * is only linked when an application actually calls
 * wtls_client_cred_load); the reference counting lives here as static
 * inline functions so the TLS connection code can hold and release
 * references without dragging the parsing code in.
 *
 * Single-threaded like the whole API. */
#ifndef WTLS_CRED_H
#define WTLS_CRED_H

#include <stdint.h>

#include <mbedtls/platform.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>

#include <wiitls/wiitls.h>

struct wtls_client_cred {
    mbedtls_x509_crt chain; /* parsed chain, leaf first; kept for own_cert */
    mbedtls_pk_context key; /* private key; PSA key slot, zeroized on free */
    uint8_t fp[32];         /* SHA-256 of the leaf DER; pool/ticket key part */
    uint32_t refs;          /* wtls_client_cred_load + one per connection */
};

/* One more connection-held reference (call before wiring own_cert). */
static inline void wtls_client_cred_retain(const wtls_client_cred *cred) {
    ((struct wtls_client_cred *)cred)->refs++;
}

/* Drops a reference; destroys the material (and zeroizes the private key
 * through mbedtls_pk_free, which destroys the PSA key) at zero. */
static inline void wtls_client_cred_release(const wtls_client_cred *cred_) {
    struct wtls_client_cred *cred = (struct wtls_client_cred *)cred_;
    if (!cred || --cred->refs > 0) {
        return;
    }
    mbedtls_pk_free(&cred->key);
    mbedtls_x509_crt_free(&cred->chain);
    mbedtls_platform_zeroize(cred, sizeof *cred);
    mbedtls_free(cred);
}

#endif
