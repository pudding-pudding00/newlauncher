#include "wtls_jitter.h"

#include <string.h>

/* Built-in SHA-256, not PSA: this runs inside psa_crypto_init() (entropy driver).
 * Private in Mbed TLS 4.x; the version is pinned (4.1.1), so the API is stable. */
#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#include <mbedtls/platform_util.h>
#include <mbedtls/private/sha256.h>

#include "wtls_internal.h"

static bool g_warmup_pending = true;

void wtls_jitter_reset_warmup(bool pending) { g_warmup_pending = pending; }

int wtls_jitter_collect_ex(wtls_sample_fn fn, void *ctx, uint8_t *out, size_t len,
                           wtls_jitter_stats *stats) {
    if (!fn || (!out && len)) return WTLS_ERR_ARG;
    if (stats) memset(stats, 0, sizeof *stats);
    if (g_warmup_pending) {
        for (int i = 0; i < WTLS_JITTER_WARMUP; i++) (void)fn(ctx);
        g_warmup_pending = false;
    }

    /* Repetition Count Test */
    uint8_t rct_val = 0;
    unsigned rct_count = 0;
    /* Adaptive Proportion Test */
    uint8_t apt_ref = 0;
    unsigned apt_count = 0, apt_pos = 0;

    for (size_t i = 0; i < len; i++) {
        uint8_t b = (uint8_t)fn(ctx);

        if (i > 0 && b == rct_val) {
            ++rct_count;
            if (stats && rct_count > stats->max_rct) stats->max_rct = rct_count;
            if (rct_count >= WTLS_RCT_CUTOFF) return WTLS_ERR_ENTROPY;
        } else {
            rct_val = b;
            rct_count = 1;
            if (stats && rct_count > stats->max_rct) stats->max_rct = rct_count;
        }

        if (apt_pos == 0) {
            apt_ref = b;
            apt_count = 1;
        } else if (b == apt_ref) {
            ++apt_count;
            if (stats && apt_count > stats->max_apt) stats->max_apt = apt_count;
            if (apt_count >= WTLS_APT_CUTOFF) return WTLS_ERR_ENTROPY;
        }
        if (stats && apt_count > stats->max_apt) stats->max_apt = apt_count;
        if (++apt_pos == WTLS_APT_WINDOW) apt_pos = 0;
        if (stats) stats->samples++;

        out[i] = b;
    }
    return WTLS_OK;
}

int wtls_jitter_collect(wtls_sample_fn fn, void *ctx, uint8_t *out, size_t len) {
    return wtls_jitter_collect_ex(fn, ctx, out, len, NULL);
}

#define RAW_PER_BLOCK (2 * 256 / WTLS_JITTER_BITS_PER_SAMPLE)

int wtls_jitter_conditioned(wtls_sample_fn fn, void *ctx, uint8_t *out, size_t len) {
    static uint8_t raw[RAW_PER_BLOCK];
    uint8_t digest[32];
    int ret = WTLS_OK;
    while (len > 0) {
        ret = wtls_jitter_collect(fn, ctx, raw, sizeof raw);
        if (ret != WTLS_OK) break;
        if (mbedtls_sha256(raw, sizeof raw, digest, 0) != 0) {
            ret = WTLS_ERR_ENTROPY;
            break;
        }
        size_t n = len < sizeof digest ? len : sizeof digest;
        memcpy(out, digest, n);
        out += n;
        len -= n;
    }
    mbedtls_platform_zeroize(raw, sizeof raw);
    mbedtls_platform_zeroize(digest, sizeof digest);
    return ret;
}
