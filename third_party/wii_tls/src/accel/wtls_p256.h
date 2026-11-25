/* NIST P-256 (secp256r1) ECDSA verify with 32-bit limbs (WTLS_ACCEL_P256).
 *
 * Verify only: every input (public key, hash, signature) is public, so the
 * implementation may be variable time (Shamir's trick, wNAF, batch inversion,
 * early skips) - no secret ever reaches this code. Secret-key operations
 * (signing, key agreement) stay in Mbed TLS.
 *
 * Called from the WTLS_ACCEL_P256 patch of mbedtls_psa_ecdsa_verify_hash in
 * the builtin driver (see CMakeLists.txt); the return values match that
 * builtin's psa_status_t mapping exactly. Pinned by tests/common/crypto_tests.c
 * (host and Wii) with RFC 6979 vectors and differential tests against
 * mbedtls_ecdsa_verify.
 */
#ifndef WTLS_P256_H
#define WTLS_P256_H

#include <stddef.h>
#include <stdint.h>

#include <psa/crypto.h>

/* Verifies sig (64 bytes, r||s big-endian) over hash (hash_len bytes) with the
 * uncompressed public key pub (65 bytes, pub[0] == 0x04).
 *
 * The hash is converted per SEC 1 4.1.4 / FIPS 186-5: only the leftmost
 * min(hash_len, 32) bytes are used. r and s must lie in [1, n-1].
 *
 * @return PSA_SUCCESS, PSA_ERROR_INVALID_SIGNATURE (r/s out of range or the
 *         check fails), or PSA_ERROR_INVALID_ARGUMENT (invalid point).
 */
psa_status_t wtls_p256_verify(const uint8_t *pub, const uint8_t *hash,
                              size_t hash_len, const uint8_t *sig);

#endif
