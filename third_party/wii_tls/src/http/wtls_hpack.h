/* HPACK (RFC 7541) encoder and decoder for HTTP/2 (not installed). */
#ifndef WTLS_HPACK_H
#define WTLS_HPACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <wiitls/http.h>

#include "http/wtls_http_parse.h"
#include "http/wtls_url.h"

#define WTLS_HPACK_DEFAULT_TABLE 4096u

typedef struct {
    char *name;
    size_t name_len;
    char *value;
    size_t value_len;
} wtls_hpack_entry;

typedef struct {
    wtls_hpack_entry *entries; /* index 0 = newest (HPACK index 62) */
    size_t count;
    size_t cap;
    size_t size;     /* sum(name_len + value_len + 32) */
    size_t max_size; /* current negotiated dynamic table max (<= max_allowed) */
    size_t max_allowed;
} wtls_hpack_ctx;

/** Initializes an HPACK decoder context with max_allowed <= WTLS_HPACK_DEFAULT_TABLE. */
void wtls_hpack_init(wtls_hpack_ctx *ctx, size_t max_allowed);

/** Releases dynamic table memory held by ctx. */
void wtls_hpack_free(wtls_hpack_ctx *ctx);

/** Encodes a uint64_t integer with prefix_bits (1..8) into dst[0..cap-1]. */
int wtls_hpack_encode_int(uint8_t *dst, size_t cap, size_t *off, uint8_t prefix_mask,
                          unsigned prefix_bits, uint64_t val);

/** Decodes an HPACK integer (prefix_bits in 1..8) from [*p, end). */
int wtls_hpack_decode_int(const uint8_t **p, const uint8_t *end, unsigned prefix_bits, uint64_t *out);

/** Decodes an HPACK string literal (raw or Huffman) into a NUL-terminated buffer. */
int wtls_hpack_decode_str(const uint8_t **p, const uint8_t *end, size_t max_out,
                          char **out, size_t *out_len);

/** Encodes an HTTP/2 request header block (RFC 9113 §8.3 + RFC 7541). Caller frees *out. */
int wtls_hpack_encode_request(const char *method, const wtls_url *u, const wtls_http_header *h,
                              size_t n, bool has_content_length, size_t body_len,
                              bool want_gzip, bool expect_100, uint8_t **out, size_t *out_len);

/**
 * Decodes an HPACK response header block into *head and a NUL-terminated string arena *arena_out
 * (field names and values point into *arena_out; caller owns and frees *arena_out).
 */
int wtls_hpack_decode_head(wtls_hpack_ctx *ctx, const uint8_t *block, size_t block_len,
                           wtls_http_head *head, char **arena_out);

#endif
