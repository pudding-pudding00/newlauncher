/* Streaming gzip decoder over zlib inflate (built with Z_PREFIX). */
#include "wtls_mem.h"
#include "http/wtls_http_gzip.h"

#include <stdbool.h>
#include <stdlib.h>

#include <zlib.h>

struct wtls_gzip {
    const wtls_codec *vt;
    z_stream z;
    bool deflate;    /* false: gzip (multi-member), true: zlib deflate (single stream) */
    bool in_member;  /* input taken since the last member ended: the end of input here is a cut */
    bool need_reset; /* gzip: a member ended; the next input byte starts a new one */
    bool ended;      /* deflate: Z_STREAM_END reached */
};

static voidpf zalloc_hook(voidpf opaque, uInt items, uInt size) {
    (void)opaque;
    return wtls_calloc(items, size);
}

static void zfree_hook(voidpf opaque, voidpf p) {
    (void)opaque;
    wtls_free(p);
}

static const wtls_codec k_gzip_codec;

static wtls_gzip *alloc_inflate(int window_bits, bool deflate) {
    wtls_gzip *g = wtls_calloc(1, sizeof *g);
    if (!g) return NULL;
    g->vt = &k_gzip_codec;
    g->deflate = deflate;
    g->z.zalloc = zalloc_hook;
    g->z.zfree = zfree_hook;
    if (inflateInit2(&g->z, window_bits) != Z_OK) {
        wtls_free(g);
        return NULL;
    }
    return g;
}

wtls_gzip *wtls_gzip_new(void) { return alloc_inflate(16 + MAX_WBITS, false); }

wtls_gzip *wtls_deflate_new(void) { return alloc_inflate(MAX_WBITS, true); }

static int gz_inflate(wtls_gzip *g, const uint8_t *in, size_t in_len, size_t *used, uint8_t *out, size_t out_cap,
                      size_t *produced) {
    if (used) *used = 0;
    if (produced) *produced = 0;
    if (!g || !used || !produced || (!in && in_len) || (!out && out_cap)) return WTLS_ERR_ARG;
    for (;;) {
        if (g->ended) return *used < in_len ? WTLS_ERR_HTTP : WTLS_OK;
        if (g->need_reset) {
            if (*used == in_len) return WTLS_OK;
            if (inflateReset(&g->z) != Z_OK) return WTLS_ERR_HTTP;
            g->need_reset = false;
        }
        size_t in_left = in_len - *used, out_left = out_cap - *produced;
        /* uInt is 32 bits: hand zlib at most that much per call. */
        uInt ai = in_left > 0x40000000u ? 0x40000000u : (uInt)in_left;
        uInt ao = out_left > 0x40000000u ? 0x40000000u : (uInt)out_left;
        g->z.next_in = in ? (Bytef *)(in + *used) : NULL;
        g->z.avail_in = ai;
        g->z.next_out = out ? (out + *produced) : NULL;
        g->z.avail_out = ao;
        int zr = inflate(&g->z, Z_NO_FLUSH);
        size_t u = ai - g->z.avail_in, p = ao - g->z.avail_out;
        *used += u;
        *produced += p;
        if (u) g->in_member = true;
        switch (zr) {
        case Z_STREAM_END:
            g->in_member = false;
            if (g->deflate) {
                g->ended = true;
                if (*used < in_len) return WTLS_ERR_HTTP;
                return WTLS_OK;
            }
            g->need_reset = true;
            if (*produced) return WTLS_OK;
            continue;
        case Z_OK:
            if (*produced || *used == in_len || *produced == out_cap) return WTLS_OK;
            continue;
        case Z_BUF_ERROR: /* no progress possible: needs input or output room */
            return WTLS_OK;
        case Z_MEM_ERROR:
            return WTLS_ERR_NOMEM;
        default: /* Z_DATA_ERROR (bad header, data, CRC or length), Z_NEED_DICT, Z_STREAM_ERROR */
            return WTLS_ERR_HTTP;
        }
    }
}

static int gz_finish(const wtls_gzip *g) { return g->in_member ? WTLS_ERR_IO : WTLS_OK; }

static void gz_destroy(wtls_gzip *g) {
    inflateEnd(&g->z);
    wtls_free(g);
}

static const wtls_codec k_gzip_codec = {gz_inflate, gz_finish, gz_destroy};
