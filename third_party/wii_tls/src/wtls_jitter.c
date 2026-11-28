#include "wtls_jitter.h"

#include <string.h>

/* Built-in SHA-256, not PSA: this runs inside psa_crypto_init() (entropy driver).
 * Private in Mbed TLS 4.x; the version is pinned (4.1.1), so the API is stable. */
#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#include <mbedtls/platform_util.h>
#include <mbedtls/private/sha256.h>

#include "wtls_internal.h"

static unsigned g_warmup_left = WTLS_JITTER_WARMUP;

void wtls_jitter_reset_warmup(bool pending) { g_warmup_left = pending ? WTLS_JITTER_WARMUP : 0; }

int wtls_jitter_collect_ex(wtls_sample_fn fn, void *ctx, uint8_t *out, size_t len,
                           wtls_jitter_stats *stats) {
    if (!fn || (!out && len)) return WTLS_ERR_ARG;
    if (stats) memset(stats, 0, sizeof *stats);
    for (; g_warmup_left > 0; g_warmup_left--) (void)fn(ctx);

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

void wtls_jitter_block_clear(wtls_jitter_block *b) { mbedtls_platform_zeroize(b, sizeof *b); }

/* The per-sample tests of wtls_jitter_collect_ex(), with their state in b so
 * a block can span calls; b->n == 0 starts them afresh, as a new call does. */
int wtls_jitter_block_step(wtls_jitter_block *b, wtls_sample_fn fn, void *ctx,
                           size_t max_samples, uint8_t out[32]) {
    if (!b || !fn || !out) return WTLS_ERR_ARG;
    for (; g_warmup_left > 0 && max_samples > 0; g_warmup_left--, max_samples--) (void)fn(ctx);
    for (; b->n < sizeof b->raw && max_samples > 0; max_samples--) {
        uint8_t v = (uint8_t)fn(ctx);
        if (b->n > 0 && v == b->rct_val) {
            if (++b->rct_count >= WTLS_RCT_CUTOFF) goto fail;
        } else {
            b->rct_val = v;
            b->rct_count = 1;
        }
        if (b->apt_pos == 0) {
            b->apt_ref = v;
            b->apt_count = 1;
        } else if (v == b->apt_ref && ++b->apt_count >= WTLS_APT_CUTOFF) {
            goto fail;
        }
        if (++b->apt_pos == WTLS_APT_WINDOW) b->apt_pos = 0;
        b->raw[b->n++] = v;
    }
    if (b->n < sizeof b->raw) return WTLS_WANT_READ;
    int r = mbedtls_sha256(b->raw, sizeof b->raw, out, 0);
    wtls_jitter_block_clear(b);
    return r == 0 ? WTLS_OK : WTLS_ERR_ENTROPY;
fail:
    wtls_jitter_block_clear(b);
    return WTLS_ERR_ENTROPY;
}

int wtls_jitter_conditioned(wtls_sample_fn fn, void *ctx, uint8_t *out, size_t len) {
    static wtls_jitter_block blk;
    uint8_t digest[32];
    int ret = WTLS_OK;
    wtls_jitter_block_clear(&blk);
    while (len > 0) {
        ret = wtls_jitter_block_step(&blk, fn, ctx, SIZE_MAX, digest);
        if (ret != WTLS_OK) break;
        size_t n = len < sizeof digest ? len : sizeof digest;
        memcpy(out, digest, n);
        out += n;
        len -= n;
    }
    mbedtls_platform_zeroize(digest, sizeof digest);
    return ret;
}
