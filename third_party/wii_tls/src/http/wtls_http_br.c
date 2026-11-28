/* Brotli (RFC 7932) Content-Encoding decoder on the reference decoder. The window is capped at
 * 2^WTLS_BROTLI_MAX_LGWIN bytes (default 4 MiB, what encoders use by default): a server could
 * otherwise make the decoder allocate up to 16 MiB. */
#include <string.h>

#include <brotli/decode.h>

#include "http/wtls_http_gzip.h"
#include "wtls_mem.h"

#ifndef WTLS_BROTLI_MAX_LGWIN
#define WTLS_BROTLI_MAX_LGWIN 22 /* 4 MiB: the Brotli encoder's own default window */
#endif

struct wtls_gzip {
    const wtls_codec *vt;
    BrotliDecoderState *st;
    bool seen_input;
    bool ended;
    bool checked_window;
};

static void *br_alloc(void *opaque, size_t size) {
    (void)opaque;
    return wtls_malloc(size);
}

static void br_free(void *opaque, void *p) {
    (void)opaque;
    wtls_free(p);
}

/* RFC 7932 9.1: the window size is in the first bits of the stream. */
static int window_bits(uint8_t b0) {
    if ((b0 & 1) == 0) return 16;
    unsigned n = (b0 >> 1) & 7;
    if (n != 0) return 17 + (int)n;
    unsigned m = (b0 >> 4) & 7;
    if (m == 0) return 17;
    if (m == 1) return 99; /* large-window brotli: not negotiated, not accepted */
    return 8 + (int)m;
}

static int br_inflate(wtls_gzip *g, const uint8_t *in, size_t in_len, size_t *used, uint8_t *out, size_t out_cap,
                      size_t *produced) {
    if (g->ended) return in_len ? WTLS_ERR_HTTP : WTLS_OK;
    if (!g->checked_window && in_len) {
        g->checked_window = true;
        if (window_bits(in[0]) > WTLS_BROTLI_MAX_LGWIN) return WTLS_ERR_HTTP_LIMIT;
    }
    if (in_len) g->seen_input = true;
    size_t avail_in = in_len, avail_out = out_cap;
    const uint8_t *next_in = in;
    uint8_t *next_out = out;
    BrotliDecoderResult r = BrotliDecoderDecompressStream(g->st, &avail_in, &next_in, &avail_out, &next_out, NULL);
    *used = in_len - avail_in;
    *produced = out_cap - avail_out;
    switch (r) {
    case BROTLI_DECODER_RESULT_SUCCESS:
        g->ended = true;
        return avail_in ? WTLS_ERR_HTTP : WTLS_OK; /* bytes after the stream */
    case BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT:
    case BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT:
        return WTLS_OK;
    default:
        return BrotliDecoderGetErrorCode(g->st) == BROTLI_DECODER_ERROR_ALLOC_CONTEXT_MODES ? WTLS_ERR_NOMEM : WTLS_ERR_HTTP;
    }
}

static int br_finish(const wtls_gzip *g) { return g->seen_input && !g->ended ? WTLS_ERR_IO : WTLS_OK; }

static void br_destroy(wtls_gzip *g) {
    BrotliDecoderDestroyInstance(g->st);
    wtls_free(g);
}

static const wtls_codec k_br_codec = {br_inflate, br_finish, br_destroy};

wtls_gzip *wtls_br_new(void) {
    wtls_gzip *g = wtls_calloc(1, sizeof *g);
    if (!g) return NULL;
    g->vt = &k_br_codec;
    g->st = BrotliDecoderCreateInstance(br_alloc, br_free, NULL);
    if (!g->st) {
        wtls_free(g);
        return NULL;
    }
    return g;
}
