/* Incremental, in-place decoder for Transfer-Encoding: chunked (RFC 9112 §7.1). */
#ifndef WTLS_HTTP_CHUNKED_H
#define WTLS_HTTP_CHUNKED_H

#include <stddef.h>
#include <stdint.h>

#include <wiitls/wiitls.h>

#define WTLS_HTTP_MAX_CHUNK_LINE 1024u /* chunk-size line incl. extensions, without its line end */
#define WTLS_HTTP_MAX_TRAILERS 16384u  /* trailer section */

typedef struct {
    uint8_t state;
    uint64_t remaining;   /* chunk-size, then data bytes still to come */
    uint32_t line_len;    /* bytes of the current chunk-size line */
    uint32_t trailer_len; /* bytes of the trailer section so far */
    uint8_t digits;       /* hex digits read (0 => empty size) */
} wtls_chunked;

void wtls_chunked_init(wtls_chunked *d);

/* Decodes buf[0..len) in place: the payload lands in buf[0..*out).
 * WTLS_WANT_READ: needs more input. WTLS_OK: message complete; the *rest raw
 * bytes after it stay in buf[len - *rest .. len) (later calls return WTLS_OK,
 * *out 0, *rest len). WTLS_ERR_HTTP | WTLS_ERR_HTTP_LIMIT; the decoder then
 * stays failed. Trailer fields are skipped. */
int wtls_chunked_decode(wtls_chunked *d, uint8_t *buf, size_t len, size_t *out, size_t *rest);

#endif
