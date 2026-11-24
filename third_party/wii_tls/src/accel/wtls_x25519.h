/* X25519 (RFC 7748) with 32-bit limbs, for CPUs without a 64x64 multiplier.
 *
 * Everest's HACL* code, which TF-PSA-Crypto uses for X25519, represents field
 * elements in 51-bit limbs; without __int128 (PPC32) every 64x64->128 product
 * is emulated. This implementation uses ten limbs of alternately 26 and 25
 * bits, so each product is one 32x32->64 multiply (mullw/mulhw on the
 * Broadway). Built with WTLS_ACCEL: Everest's mbedtls_x25519_scalarmult then
 * calls it for both the key agreement and the public key export. */
#ifndef WTLS_X25519_H
#define WTLS_X25519_H

#include <stdint.h>

/* out = X25519(scalar, point): clamps the scalar and ignores the top bit of
 * the point, as RFC 7748 section 5 requires. Constant time in both inputs.
 * out may alias neither input. */
void wtls_x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]);

#endif
