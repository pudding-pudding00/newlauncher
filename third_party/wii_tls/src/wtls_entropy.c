/* Entropy driver for TF-PSA-Crypto (MBEDTLS_PSA_DRIVER_GET_ENTROPY). */
#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#include "wtls_entropy.h"

#include <string.h>

#include <mbedtls/platform.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/private/sha256.h>
#include <psa/crypto.h>

#include "plat/plat.h"
#include "wtls_internal.h"
#include "wtls_log.h"

size_t wtls_entropy_test_delivered;

/* In Dolphin an IPC round trip alternates between two timebase deltas: the
 * samples pass RCT/APT but carry ~0 bits (docs/dolphin.md), so the jitter
 * source is refused there. WTLS_DOLPHIN_DEV_ENTROPY (debugging only, never in
 * release packages) lets it run anyway, with a warning: keys are predictable. */
#if WTLS_DOLPHIN_DEV_ENTROPY
/* Extern on purpose: the dolphin_dev_entropy_not_in_release link test looks
 * for this symbol. */
void wtls_dolphin_dev_entropy_warn(void);
void wtls_dolphin_dev_entropy_warn(void) {
    static bool warned;
    if (warned) return;
    warned = true;
    wtls_log(WTLS_LOG_WARN, WTLS_LOG_CAT_PLATFORM,
             "WTLS_DOLPHIN_DEV_ENTROPY: emulator detected, its jitter carries NO entropy - keys are predictable");
}
#endif

static bool jitter_refused(void) {
    if (!wtls_plat_entropy_emulated()) return false;
#if WTLS_DOLPHIN_DEV_ENTROPY
    wtls_dolphin_dev_entropy_warn();
    return false;
#else
    return true;
#endif
}

#if defined(WTLS_PLATFORM_HOST)
static wtls_sample_fn s_sampler;
static void *s_sampler_ctx;

void wtls_entropy_set_test_sampler(wtls_sample_fn fn, void *ctx) {
    s_sampler = fn;
    s_sampler_ctx = ctx;
    wtls_entropy_pool_clear();
}
#else
static uint32_t jitter_cb(void *ctx) {
    (void)ctx;
    return wtls_plat_jitter_sample();
}
static const wtls_sample_fn s_sampler = jitter_cb;
static void *const s_sampler_ctx = NULL;

void wtls_entropy_set_test_sampler(wtls_sample_fn fn, void *ctx) {
    (void)fn;
    (void)ctx;
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

/* Conditioned bytes, produced by wtls_jitter_conditioned() exactly like the
 * inline path; each byte is handed out once and wiped. */
static uint8_t s_pool[WTLS_ENTROPY_POOL_BYTES];
static size_t s_pool_len;
static wtls_jitter_block s_blk; /* the block being collected across steps */

_Static_assert(WTLS_ENTROPY_POOL_BYTES % 32 == 0, "the pool holds whole conditioned blocks");

void wtls_entropy_pool_clear(void) {
    mbedtls_platform_zeroize(s_pool, sizeof s_pool);
    s_pool_len = 0;
    wtls_jitter_block_clear(&s_blk);
}

int wtls_entropy_pool_fill(uint32_t budget_ms) {
    if (!s_sampler) return WTLS_OK; /* host OS entropy: nothing to collect ahead */
    if (jitter_refused()) {
        wtls_entropy_pool_clear();
        return WTLS_ERR_ENTROPY;
    }
    uint64_t deadline = wtls_plat_now_ms() + budget_ms;
    while (s_pool_len < sizeof s_pool) {
        int r = wtls_jitter_block_step(&s_blk, s_sampler, s_sampler_ctx, WTLS_ENTROPY_SLICE_SAMPLES,
                                       s_pool + s_pool_len);
        if (r == WTLS_OK) {
            s_pool_len += 32;
        } else if (r != WTLS_WANT_READ) {
            wtls_entropy_pool_clear();
            return WTLS_ERR_ENTROPY;
        }
        if (s_pool_len < sizeof s_pool && wtls_plat_now_ms() >= deadline) return WTLS_WANT_READ;
    }
    return WTLS_OK;
}

int mbedtls_platform_get_entropy(psa_driver_get_entropy_flags_t flags,
                                 size_t *estimate_bits,
                                 unsigned char *output, size_t output_size) {
    if (flags != 0) return PSA_ERROR_NOT_SUPPORTED;
    *estimate_bits = 0;
    if (!s_sampler) {
        if (wtls_plat_os_entropy(output, output_size) != WTLS_OK)
            return PSA_ERROR_INSUFFICIENT_ENTROPY;
    } else if (jitter_refused()) {
        wtls_entropy_pool_clear();
        return PSA_ERROR_INSUFFICIENT_ENTROPY;
    } else {
        size_t take = output_size < s_pool_len ? output_size : s_pool_len;
        memcpy(output, s_pool + s_pool_len - take, take);
        mbedtls_platform_zeroize(s_pool + s_pool_len - take, take);
        s_pool_len -= take;
        int r = wtls_jitter_conditioned(s_sampler, s_sampler_ctx, output + take, output_size - take);
#if !defined(WTLS_PLATFORM_HOST)
        if (r == WTLS_OK && output_size > 0 && mix_aux(output, output_size) != 0) r = WTLS_ERR_ENTROPY;
#endif
        if (r != WTLS_OK) {
            mbedtls_platform_zeroize(output, output_size);
            return PSA_ERROR_INSUFFICIENT_ENTROPY; /* fail closed: psa_crypto_init fails */
        }
    }
    /* Conditioned output: each 32-byte block is SHA-256 over 2x its size in
     * estimated raw entropy, so it counts as full entropy (aux is non-credited). */
    *estimate_bits = 8 * output_size;
    wtls_entropy_test_delivered += output_size;
    return 0;
}
