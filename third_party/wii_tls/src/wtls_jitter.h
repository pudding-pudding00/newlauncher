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
/* Provisional until the Task 2 spike ran on hardware (docs/ios-socket-behaviour.md):
 * conservative floor(H/2), minimum 1. */
#define WTLS_JITTER_BITS_PER_SAMPLE 1
#define WTLS_JITTER_WARMUP 1024

/* out[i] = low byte of the i-th sample, after RCT/APT health tests that restart
 * on every call. The first call per process first discards WTLS_JITTER_WARMUP
 * samples. Returns WTLS_OK or WTLS_ERR_ENTROPY. */
int wtls_jitter_collect(wtls_sample_fn fn, void *ctx, uint8_t *out, size_t len);

/* Full-entropy output: each 32-byte block is SHA-256 over
 * 2*256/WTLS_JITTER_BITS_PER_SAMPLE health-tested raw bytes (SP 800-90C-style
 * 2x input). Returns WTLS_OK or WTLS_ERR_ENTROPY. */
int wtls_jitter_conditioned(wtls_sample_fn fn, void *ctx, uint8_t *out, size_t len);

/* Test hook: pending=true makes the next call run the warm-up again,
 * pending=false skips it. */
void wtls_jitter_reset_warmup(bool pending);

#endif
