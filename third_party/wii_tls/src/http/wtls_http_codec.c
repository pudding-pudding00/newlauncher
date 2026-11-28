/* Dispatch of the Content-Encoding decoders (gzip, deflate, br, zstd) behind wtls_gzip. */
#include "http/wtls_http_gzip.h"

typedef struct {
    const wtls_codec *vt;
} codec_head;

int wtls_gzip_inflate(wtls_gzip *g, const uint8_t *in, size_t in_len, size_t *used, uint8_t *out, size_t out_cap,
                      size_t *produced) {
    if (used) *used = 0;
    if (produced) *produced = 0;
    if (!g || !used || !produced || (!in && in_len) || (!out && out_cap)) return WTLS_ERR_ARG;
    return ((codec_head *)g)->vt->inflate(g, in, in_len, used, out, out_cap, produced);
}

int wtls_gzip_finish(const wtls_gzip *g) {
    if (!g) return WTLS_ERR_ARG;
    return ((const codec_head *)g)->vt->finish(g);
}

void wtls_gzip_free(wtls_gzip *g) {
    if (g) ((codec_head *)g)->vt->destroy(g);
}
