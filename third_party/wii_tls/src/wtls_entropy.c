/* Entropy driver for TF-PSA-Crypto (MBEDTLS_PSA_DRIVER_GET_ENTROPY). */
#include <mbedtls/platform.h>
#include <psa/crypto.h>

#include "plat/plat.h"
#include "wtls_internal.h"
#include "wtls_jitter.h"

#if !defined(WTLS_PLATFORM_HOST)
static uint32_t jitter_cb(void *ctx) {
    (void)ctx;
    return wtls_plat_jitter_sample();
}
#endif

int mbedtls_platform_get_entropy(psa_driver_get_entropy_flags_t flags,
                                 size_t *estimate_bits,
                                 unsigned char *output, size_t output_size) {
    if (flags != 0) return PSA_ERROR_NOT_SUPPORTED;
#if defined(WTLS_PLATFORM_HOST)
    if (wtls_plat_os_entropy(output, output_size) != WTLS_OK)
        return PSA_ERROR_INSUFFICIENT_ENTROPY;
    *estimate_bits = 8 * output_size;
    return 0;
#else
    int r = wtls_jitter_conditioned(jitter_cb, NULL, output, output_size);
    wtls_plat_jitter_release();
    if (r != WTLS_OK) {
        *estimate_bits = 0;
        return PSA_ERROR_INSUFFICIENT_ENTROPY; /* fail closed: psa_crypto_init fails */
    }
    /* Conditioned output: each 32-byte block is SHA-256 over 2x its size in
     * estimated raw entropy, so it counts as full entropy. */
    *estimate_bits = 8 * output_size;
    return 0;
#endif
}
