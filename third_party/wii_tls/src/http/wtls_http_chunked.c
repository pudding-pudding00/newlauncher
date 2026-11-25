/* Incremental, in-place decoder for Transfer-Encoding: chunked (RFC 9112 §7.1).
 *
 * The write position never overtakes the read position, so the payload can be
 * compacted to the start of the same buffer. Line ends are CRLF or bare LF. */
#include "http/wtls_http_chunked.h"

#include <string.h>

enum {
    S_SIZE,       /* hex digits of chunk-size */
    S_EXT,        /* chunk-ext up to the line end */
    S_SIZE_LF,    /* CR seen on the size line */
    S_DATA,       /* remaining data bytes */
    S_DATA_END,   /* CRLF / LF after the data */
    S_DATA_LF,    /* CR seen after the data */
    S_TRAILER,    /* start of a trailer line (or the final empty line) */
    S_TRAILER_IN, /* inside a trailer line */
    S_TRAILER_LF, /* CR seen inside a trailer line */
    S_END_LF,     /* CR seen on the final empty line */
    S_DONE,
    S_FAILED,
};

void wtls_chunked_init(wtls_chunked *d) { memset(d, 0, sizeof *d); }

static int hexval(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int fail(wtls_chunked *d, int err) {
    d->state = S_FAILED;
    return err;
}

/* End of the size line: data or trailers next. */
static void size_line_done(wtls_chunked *d) {
    d->line_len = 0;
    d->digits = 0;
    d->state = d->remaining ? S_DATA : S_TRAILER;
}

int wtls_chunked_decode(wtls_chunked *d, uint8_t *buf, size_t len, size_t *out, size_t *rest) {
    size_t i = 0, o = 0;
    *out = 0;
    *rest = 0;
    if (d->state == S_FAILED) return WTLS_ERR_HTTP;
    while (i < len && d->state != S_DONE) {
        if (d->state == S_DATA) {
            size_t n = len - i;
            if (n > d->remaining) n = (size_t)d->remaining;
            if (o != i) memmove(buf + o, buf + i, n);
            o += n, i += n;
            d->remaining -= n;
            if (!d->remaining) d->state = S_DATA_END;
            continue;
        }
        uint8_t c = buf[i++];
        switch (d->state) {
        case S_SIZE:
        case S_EXT:
            if (c == '\r' || c == '\n') {
                if (!d->digits) return fail(d, WTLS_ERR_HTTP);
                if (c == '\r') d->state = S_SIZE_LF;
                else size_line_done(d);
                break;
            }
            if (++d->line_len > WTLS_HTTP_MAX_CHUNK_LINE) return fail(d, WTLS_ERR_HTTP_LIMIT);
            if (d->state == S_EXT) {
                if (c < 0x20 && c != '\t') return fail(d, WTLS_ERR_HTTP);
                break;
            }
            {
                int v = hexval(c);
                if (v >= 0) {
                    if (d->remaining > (UINT64_MAX >> 4)) return fail(d, WTLS_ERR_HTTP);
                    d->remaining = (d->remaining << 4) | (uint64_t)v;
                    d->digits = 1;
                } else if (d->digits && (c == ';' || c == ' ' || c == '\t')) {
                    d->state = S_EXT;
                } else {
                    return fail(d, WTLS_ERR_HTTP);
                }
            }
            break;
        case S_SIZE_LF:
            if (c != '\n') return fail(d, WTLS_ERR_HTTP);
            size_line_done(d);
            break;
        case S_DATA_END:
            if (c == '\r') d->state = S_DATA_LF;
            else if (c == '\n') d->state = S_SIZE;
            else return fail(d, WTLS_ERR_HTTP);
            break;
        case S_DATA_LF:
            if (c != '\n') return fail(d, WTLS_ERR_HTTP);
            d->state = S_SIZE;
            break;
        default: /* trailer section */
            if (++d->trailer_len > WTLS_HTTP_MAX_TRAILERS) return fail(d, WTLS_ERR_HTTP_LIMIT);
            switch (d->state) {
            case S_TRAILER:
                if (c == '\r') d->state = S_END_LF;
                else if (c == '\n') d->state = S_DONE;
                else d->state = S_TRAILER_IN;
                break;
            case S_TRAILER_IN:
                if (c == '\r') d->state = S_TRAILER_LF;
                else if (c == '\n') d->state = S_TRAILER;
                break;
            case S_TRAILER_LF:
                if (c != '\n') return fail(d, WTLS_ERR_HTTP);
                d->state = S_TRAILER;
                break;
            case S_END_LF:
                if (c != '\n') return fail(d, WTLS_ERR_HTTP);
                d->state = S_DONE;
                break;
            }
            break;
        }
    }
    *out = o;
    if (d->state == S_DONE) {
        *rest = len - i;
        return WTLS_OK;
    }
    return WTLS_WANT_READ;
}
