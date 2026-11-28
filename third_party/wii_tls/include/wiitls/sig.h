/**
 * @file sig.h
 * @brief Integrity helpers for downloaded payloads: SHA-256 and ECDSA P-256
 *        signature verification against a public key embedded in the app.
 *
 * Verification only: the signing key never leaves your build machine
 * (tools/sign_payload.py). Needs wtls_init(). Same threading rule as wiitls.h.
 */
#ifndef WIITLS_SIG_H
#define WIITLS_SIG_H

#include <wiitls/wiitls.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Signature encodings accepted by the verify functions. */
typedef enum {
    WTLS_SIG_RAW = 0, /**< 64 bytes: r || s, big endian */
    WTLS_SIG_DER = 1, /**< ASN.1 DER ECDSA-Sig-Value (what `openssl dgst -sign` writes); must be canonical */
} wtls_sig_format;

/**
 * Verifies an ECDSA P-256 / SHA-256 signature over @p msg.
 *
 * @p pubkey is either the 65-byte uncompressed point (0x04 || X || Y) or a DER
 * SubjectPublicKeyInfo of a P-256 key (91 bytes, what `openssl ec -pubout
 * -outform DER` writes). Fixed-width raw signatures are exactly 64 bytes; the
 * format is stated explicitly because a 64-byte DER signature exists.
 *
 * @return WTLS_OK; WTLS_ERR_INTEGRITY (the signature is wrong, including
 *         r or s out of range and non-canonical DER); WTLS_ERR_ARG (NULL,
 *         malformed public key or signature framing); WTLS_ERR_STATE
 *         (wtls_init() not called).
 */
wtls_status wtls_sig_verify_p256(const uint8_t *pubkey, size_t pubkey_len,
                                 const uint8_t *msg, size_t msg_len,
                                 const uint8_t *sig, size_t sig_len, wtls_sig_format fmt);

/** Like wtls_sig_verify_p256() over an already computed SHA-256 digest (large files). */
wtls_status wtls_sig_verify_p256_digest(const uint8_t *pubkey, size_t pubkey_len,
                                        const uint8_t sha256[32],
                                        const uint8_t *sig, size_t sig_len, wtls_sig_format fmt);

/** Incremental SHA-256. Zero-initialisation is not enough: call wtls_sha256_init(). */
typedef struct {
    uint64_t opaque[48];
} wtls_sha256_ctx;

/** @return WTLS_OK, WTLS_ERR_ARG, or WTLS_ERR_STATE (wtls_init() not called). */
wtls_status wtls_sha256_init(wtls_sha256_ctx *c);
/** @return WTLS_OK or WTLS_ERR_ARG. */
wtls_status wtls_sha256_update(wtls_sha256_ctx *c, const void *p, size_t n);
/** Writes the digest and invalidates @p c (call wtls_sha256_init() to reuse it). */
wtls_status wtls_sha256_finish(wtls_sha256_ctx *c, uint8_t out[32]);
/** Abandons a computation without producing a digest; safe on a finished context. */
void wtls_sha256_abort(wtls_sha256_ctx *c);

#ifdef __cplusplus
}
#endif

#endif
