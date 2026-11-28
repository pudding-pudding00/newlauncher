/* Entropy pool for wtls_init_start()/wtls_init_step(): conditioned jitter
 * output collected ahead of psa_crypto_init() in time slices. */
#ifndef WTLS_ENTROPY_H
#define WTLS_ENTROPY_H

#include <stddef.h>
#include <stdint.h>

#include "wtls_jitter.h"

/* Conditioned bytes collected ahead: covers every request psa_crypto_init()
 * makes (256 bytes with the pinned Mbed TLS; test_init_step checks this), so
 * the init runs from the pool. */
#define WTLS_ENTROPY_POOL_BYTES 256

/* Raw samples between two deadline checks in wtls_entropy_pool_fill():
 * ~1.3 ms on the Wii (one IOS IPC round trip each). */
#define WTLS_ENTROPY_SLICE_SAMPLES 16

/* Collects raw samples (warm-up first) into conditioned blocks in the pool
 * until it is full or budget_ms has passed, checking the deadline every
 * WTLS_ENTROPY_SLICE_SAMPLES samples; at least one slice per call while not
 * full. A partial block carries over to the next call.
 * WTLS_OK (full) | WTLS_WANT_READ (call again) | WTLS_ERR_ENTROPY (health
 * test failed: the pool is wiped). */
int wtls_entropy_pool_fill(uint32_t budget_ms);

/* Wipes the pool (shutdown, failed init). */
void wtls_entropy_pool_clear(void);

/* Test hooks. A sampler set here makes the host take the jitter path the Wii
 * takes (NULL: back to OS entropy); the counter adds up the bytes handed to
 * PSA by mbedtls_platform_get_entropy(). */
void wtls_entropy_set_test_sampler(wtls_sample_fn fn, void *ctx);
extern size_t wtls_entropy_test_delivered;

#endif
