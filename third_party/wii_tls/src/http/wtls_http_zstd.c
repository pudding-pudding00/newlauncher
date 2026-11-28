/* zstd (RFC 8878) Content-Encoding decoder. Concatenated frames are decoded one after another;
 * a window above 2^WTLS_ZSTD_MAX_WINDOW_LOG bytes (default 8 MiB, the HTTP limit of RFC 9659) is refused. */
#include <string.h>

#include <zstd.h>
#include <zstd_errors.h>

#include "http/wtls_http_gzip.h"
#include "wtls_mem.h"

#ifndef WTLS_ZSTD_MAX_WINDOW_LOG
#define WTLS_ZSTD_MAX_WINDOW_LOG 23 /* RFC 9659: at most 8 MiB for Content-Encoding: zstd */
#endif

struct wtls_gzip {
    const wtls_codec *vt;
    ZSTD_DCtx *dctx;
    bool seen_input;
    bool in_frame; /* the last call left a frame unfinished */
};

static void *z_alloc(void *opaque, size_t size) {
    (void)opaque;
    return wtls_malloc(size);
}

static void z_free(void *opaque, void *p) {
    (void)opaque;
    wtls_free(p);
}

static int z_inflate(wtls_gzip *g, const uint8_t *in, size_t in_len, size_t *used, uint8_t *out, size_t out_cap,
                     size_t *produced) {
    if (in_len) g->seen_input = true;
    ZSTD_inBuffer ib = {in, in_len, 0};
    ZSTD_outBuffer ob = {out, out_cap, 0};
    /* Run until output appeared, all input is taken, or the output buffer is full. */
    for (;;) {
        size_t r = ZSTD_decompressStream(g->dctx, &ob, &ib);
        if (ZSTD_isError(r)) {
            *used = ib.pos;
            *produced = ob.pos;
            ZSTD_ErrorCode e = ZSTD_getErrorCode(r);
            if (e == ZSTD_error_frameParameter_windowTooLarge) return WTLS_ERR_HTTP_LIMIT;
            return e == ZSTD_error_memory_allocation ? WTLS_ERR_NOMEM : WTLS_ERR_HTTP;
        }
        if (ib.pos > 0 || ob.pos > 0) g->in_frame = r != 0; /* an empty call after a frame only returns a hint for the next header */
        if (ob.pos > 0 || ib.pos == ib.size || ob.pos == ob.size) break;
        if (r == 0 && ib.pos < ib.size) continue; /* a frame ended; more input may start the next one */
        break;
    }
    *used = ib.pos;
    *produced = ob.pos;
    return WTLS_OK;
}

static int z_finish(const wtls_gzip *g) { return g->seen_input && g->in_frame ? WTLS_ERR_IO : WTLS_OK; }

static void z_destroy(wtls_gzip *g) {
    ZSTD_freeDCtx(g->dctx);
    wtls_free(g);
}

static const wtls_codec k_zstd_codec = {z_inflate, z_finish, z_destroy};

wtls_gzip *wtls_zstd_new(void) {
    wtls_gzip *g = wtls_calloc(1, sizeof *g);
    if (!g) return NULL;
    g->vt = &k_zstd_codec;
    ZSTD_customMem mem = {z_alloc, z_free, NULL};
    g->dctx = ZSTD_createDCtx_advanced(mem);
    if (!g->dctx || ZSTD_isError(ZSTD_DCtx_setParameter(g->dctx, ZSTD_d_windowLogMax, WTLS_ZSTD_MAX_WINDOW_LOG))) {
        if (g->dctx) ZSTD_freeDCtx(g->dctx);
        wtls_free(g);
        return NULL;
    }
    return g;
}
