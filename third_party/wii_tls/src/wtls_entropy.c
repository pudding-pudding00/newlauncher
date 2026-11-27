/* Entropy driver for TF-PSA-Crypto (MBEDTLS_PSA_DRIVER_GET_ENTROPY). */
#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#include <string.h>

#include <mbedtls/platform.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/private/sha256.h>
#include <psa/crypto.h>

#include "plat/plat.h"
#include "wtls_internal.h"
#include "wtls_jitter.h"

#if !defined(WTLS_PLATFORM_HOST)

static uint32_t jitter_cb(void *ctx) {
    (void)ctx;
    return wtls_plat_jitter_sample();
}

static int mix_aux(uint8_t *buf, size_t len) {
    uint32_t aux[4];
    wtls_plat_entropy_aux(aux);
    uint8_t in[4 + 32 + sizeof aux];
    uint8_t digest[32];
    int ret = 0;
    for (size_t off = 0, blk = 0; off < len; off += 32, blk++) {
        size_t chunk = len - off < 32 ? len - off : 32;
        in[0] = (uint8_t)(blk >> 24);
        in[1] = (uint8_t)(blk >> 16);
        in[2] = (uint8_t)(blk >> 8);
        in[3] = (uint8_t)blk;
        memcpy(in + 4, buf + off, chunk);
        if (chunk < 32) memset(in + 4 + chunk, 0, 32 - chunk);
        memcpy(in + 4 + 32, aux, sizeof aux);
        if (mbedtls_sha256(in, sizeof in, digest, 0) != 0) {
            ret = -1;
            break;
        }
        memcpy(buf + off, digest, chunk);
    }
    mbedtls_platform_zeroize(in, sizeof in);
    mbedtls_platform_zeroize(digest, sizeof digest);
    mbedtls_platform_zeroize(aux, sizeof aux);
    return ret;
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
    if (r != WTLS_OK || (output_size > 0 && mix_aux(output, output_size) != 0)) {
        *estimate_bits = 0;
        return PSA_ERROR_INSUFFICIENT_ENTROPY; /* fail closed: psa_crypto_init fails */
    }
    /* Conditioned output: each 32-byte block is SHA-256 over 2x its size in
     * estimated raw entropy, so it counts as full entropy (aux is non-credited). */
    *estimate_bits = 8 * output_size;
    return 0;
#endif
}
