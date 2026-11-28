/* Streaming Content-Encoding decoders (not installed): gzip/deflate (zlib), and with WTLS_BROTLI / WTLS_ZSTD
 * br and zstd, behind one interface. wtls_gzip is the generic handle of all of them.
 *
 * gzip (RFC 1952):
 *
 * Concatenated members are decoded one after another, like zcat. Built on the
 * inflate part of zlib, compiled into libwiitls with Z_PREFIX (WTLS_GZIP). */
#ifndef WTLS_HTTP_GZIP_H
#define WTLS_HTTP_GZIP_H

#include <stddef.h>
#include <stdint.h>

#include <wiitls/wiitls.h>

typedef struct wtls_gzip wtls_gzip;

/* How a decoder is driven; every decoder struct starts with a pointer to its table. */
typedef struct {
    int (*inflate)(wtls_gzip *g, const uint8_t *in, size_t in_len, size_t *used, uint8_t *out, size_t out_cap,
                   size_t *produced);
    int (*finish)(const wtls_gzip *g);
    void (*destroy)(wtls_gzip *g);
} wtls_codec;

/* Brotli (RFC 7932), window at most 2^WTLS_BROTLI_MAX_LGWIN (default 4 MiB; larger: WTLS_ERR_HTTP_LIMIT). NULL: out of memory. */
wtls_gzip *wtls_br_new(void);
/* zstd (RFC 8878), concatenated frames, window at most 2^WTLS_ZSTD_MAX_WINDOW_LOG (default 8 MiB; larger: WTLS_ERR_HTTP_LIMIT). NULL: out of memory. */
wtls_gzip *wtls_zstd_new(void);

/* NULL: out of memory. About 7 KiB of state plus a 32 KiB window. */
wtls_gzip *wtls_gzip_new(void);

/* Streaming deflate (RFC 1950 zlib format) decoder for Content-Encoding: deflate. */
wtls_gzip *wtls_deflate_new(void);

/* Decodes from in[0..in_len) into out[0..out_cap). On WTLS_OK *produced > 0 or
 * all input was taken (*used == in_len); with no input it still hands out
 * output that did not fit before. WTLS_OK, WTLS_ERR_HTTP (not gzip, corrupt
 * data, bad CRC or length, bytes after a member that start no new member) or
 * WTLS_ERR_NOMEM. */
int wtls_gzip_inflate(wtls_gzip *g, const uint8_t *in, size_t in_len, size_t *used, uint8_t *out, size_t out_cap,
                      size_t *produced);

/* The input ended: WTLS_OK between members (or before any input), else
 * WTLS_ERR_IO (cut inside a member). */
int wtls_gzip_finish(const wtls_gzip *g);

/* NULL is fine. */
void wtls_gzip_free(wtls_gzip *g);

#endif
