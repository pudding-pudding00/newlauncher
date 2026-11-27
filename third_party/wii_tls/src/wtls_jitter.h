/* Timebase-jitter entropy collector with SP 800-90B continuous health tests.
 * Pure code: the sample source is a callback, so host tests can drive it. */
#ifndef WTLS_JITTER_H
#define WTLS_JITTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t (*wtls_sample_fn)(void *ctx);

#define WTLS_RCT_CUTOFF   21   /* SP 800-90B 4.4.1, H=1, alpha=2^-20 */
#define WTLS_APT_WINDOW  512
#define WTLS_APT_CUTOFF  410   /* SP 800-90B table 2, H=1, W=512 */
/* Credited min-entropy per raw sample. The Wii sampler (one IOS IPC round
 * trip) measured >= 4.6 bits/sample under the worst SP 800-90B-style
 * estimator (docs/ios-socket-behaviour.md): 1 keeps a > 4x margin, and the
 * RCT/APT cutoffs above are sized for it. */
#define WTLS_JITTER_BITS_PER_SAMPLE 1
#define WTLS_JITTER_WARMUP 1024

typedef struct {
    size_t samples;   /* total post-warmup samples tested */
    unsigned max_rct; /* longest consecutive identical low-byte run seen (< WTLS_RCT_CUTOFF) */
    unsigned max_apt; /* highest same-byte count in a 512-sample window (< WTLS_APT_CUTOFF) */
} wtls_jitter_stats;

/* out[i] = low byte of the i-th sample, after RCT/APT health tests that restart
 * on every call. The first call per process first discards WTLS_JITTER_WARMUP
 * samples. Returns WTLS_OK or WTLS_ERR_ENTROPY. */
int wtls_jitter_collect(wtls_sample_fn fn, void *ctx, uint8_t *out, size_t len);

/* As wtls_jitter_collect(), optionally recording RCT/APT headroom stats. */
int wtls_jitter_collect_ex(wtls_sample_fn fn, void *ctx, uint8_t *out, size_t len,
                           wtls_jitter_stats *stats);

/* Full-entropy output: each 32-byte block is SHA-256 over
 * 2*256/WTLS_JITTER_BITS_PER_SAMPLE health-tested raw bytes (SP 800-90C-style
 * 2x input). Returns WTLS_OK or WTLS_ERR_ENTROPY. */
int wtls_jitter_conditioned(wtls_sample_fn fn, void *ctx, uint8_t *out, size_t len);

/* Test hook: pending=true makes the next call run the warm-up again,
 * pending=false skips it. */
void wtls_jitter_reset_warmup(bool pending);

#endif
