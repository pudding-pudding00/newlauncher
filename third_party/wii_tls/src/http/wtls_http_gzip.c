/* Streaming gzip decoder over zlib inflate (built with Z_PREFIX). */
#include "http/wtls_http_gzip.h"

#include <stdbool.h>
#include <stdlib.h>

#include <zlib.h>

struct wtls_gzip {
    z_stream z;
    bool in_member;  /* input taken since the last member ended: the end of input here is a cut */
    bool need_reset; /* a member ended; the next input byte starts a new one */
};

wtls_gzip *wtls_gzip_new(void) {
    wtls_gzip *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    if (inflateInit2(&g->z, 16 + MAX_WBITS) != Z_OK) { /* 16: gzip wrapper only, no zlib/raw */
        free(g);
        return NULL;
    }
    return g;
}

int wtls_gzip_inflate(wtls_gzip *g, const uint8_t *in, size_t in_len, size_t *used, uint8_t *out, size_t out_cap,
                      size_t *produced) {
    *used = *produced = 0;
    for (;;) {
        if (g->need_reset) {
            if (*used == in_len) return WTLS_OK;
            if (inflateReset(&g->z) != Z_OK) return WTLS_ERR_HTTP;
            g->need_reset = false;
        }
        size_t in_left = in_len - *used, out_left = out_cap - *produced;
        /* uInt is 32 bits: hand zlib at most that much per call. */
        uInt ai = in_left > 0x40000000u ? 0x40000000u : (uInt)in_left;
        uInt ao = out_left > 0x40000000u ? 0x40000000u : (uInt)out_left;
        g->z.next_in = (Bytef *)(in + *used);
        g->z.avail_in = ai;
        g->z.next_out = out + *produced;
        g->z.avail_out = ao;
        int zr = inflate(&g->z, Z_NO_FLUSH);
        size_t u = ai - g->z.avail_in, p = ao - g->z.avail_out;
        *used += u;
        *produced += p;
        if (u) g->in_member = true;
        switch (zr) {
        case Z_STREAM_END:
            g->in_member = false;
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

int wtls_gzip_finish(const wtls_gzip *g) { return g->in_member ? WTLS_ERR_IO : WTLS_OK; }

void wtls_gzip_free(wtls_gzip *g) {
    if (!g) return;
    inflateEnd(&g->z);
    free(g);
}
