/* HPACK (RFC 7541) encoder and decoder for HTTP/2. */
#include "http/wtls_hpack.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http/wtls_http_req.h"

/* ---- RFC 7541 Appendix A: Static Table (indices 1..61) -------------------- */

typedef struct {
    const char *name;
    const char *value;
} hpack_static_entry;

static const hpack_static_entry k_static[61] = {
    {":authority", ""},
    {":method", "GET"},
    {":method", "POST"},
    {":path", "/"},
    {":path", "/index.html"},
    {":scheme", "http"},
    {":scheme", "https"},
    {":status", "200"},
    {":status", "204"},
    {":status", "206"},
    {":status", "304"},
    {":status", "400"},
    {":status", "404"},
    {":status", "500"},
    {"accept-charset", ""},
    {"accept-encoding", "gzip, deflate"},
    {"accept-language", ""},
    {"accept-ranges", ""},
    {"accept", ""},
    {"access-control-allow-origin", ""},
    {"age", ""},
    {"allow", ""},
    {"authorization", ""},
    {"cache-control", ""},
    {"content-disposition", ""},
    {"content-encoding", ""},
    {"content-language", ""},
    {"content-length", ""},
    {"content-location", ""},
    {"content-range", ""},
    {"content-type", ""},
    {"cookie", ""},
    {"date", ""},
    {"etag", ""},
    {"expect", ""},
    {"expires", ""},
    {"from", ""},
    {"host", ""},
    {"if-match", ""},
    {"if-modified-since", ""},
    {"if-none-match", ""},
    {"if-range", ""},
    {"if-unmodified-since", ""},
    {"last-modified", ""},
    {"link", ""},
    {"location", ""},
    {"max-forwards", ""},
    {"proxy-authenticate", ""},
    {"proxy-authorization", ""},
    {"range", ""},
    {"referer", ""},
    {"refresh", ""},
    {"retry-after", ""},
    {"server", ""},
    {"set-cookie", ""},
    {"strict-transport-security", ""},
    {"transfer-encoding", ""},
    {"user-agent", ""},
    {"vary", ""},
    {"via", ""},
    {"www-authenticate", ""},
};

#define HPACK_STATIC_COUNT (sizeof k_static / sizeof k_static[0])

/* ---- RFC 7541 Appendix B: Huffman Code Bit Lengths ------------------------ */

static const uint8_t k_huff_lens[257] = {
    13, 23, 28, 28, 28, 28, 28, 28, 28, 24, 30, 28, 28, 30, 28, 28,
    28, 28, 28, 28, 28, 28, 30, 28, 28, 28, 28, 28, 28, 28, 28, 28,
     6, 10, 10, 12, 13,  6,  8, 11, 10, 10,  8, 11,  8,  6,  6,  6,
     5,  5,  5,  6,  6,  6,  6,  6,  6,  6,  7,  8, 15,  6, 12, 10,
    13,  6,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  8,  7,  8, 13, 19, 13, 14,  6,
    15,  5,  6,  5,  6,  5,  6,  6,  6,  5,  7,  7,  6,  6,  6,  5,
     6,  7,  6,  5,  5,  6,  7,  7,  7,  7,  7, 15, 11, 14, 13, 28,
    20, 22, 20, 20, 22, 22, 22, 23, 22, 23, 23, 23, 23, 23, 24, 23,
    24, 24, 22, 23, 24, 23, 23, 23, 23, 21, 22, 23, 22, 23, 23, 24,
    22, 21, 20, 22, 22, 23, 23, 21, 23, 22, 22, 24, 21, 22, 23, 23,
    21, 21, 22, 21, 23, 22, 23, 23, 20, 22, 22, 22, 23, 22, 22, 23,
    26, 26, 20, 19, 22, 23, 22, 25, 26, 26, 26, 27, 27, 26, 24, 25,
    19, 21, 26, 27, 27, 26, 27, 24, 21, 21, 26, 26, 28, 27, 27, 27,
    20, 24, 20, 21, 22, 21, 21, 23, 22, 22, 25, 25, 24, 24, 26, 23,
    26, 27, 26, 26, 27, 27, 27, 27, 27, 28, 27, 27, 27, 27, 27, 26,
    30
};

typedef struct {
    bool ready;
    uint8_t count[31];
    uint32_t first[31];
    uint16_t off[31];
    uint16_t syms[257];
} huff_table;

static huff_table s_huff;

static void huff_init(void) {
    if (s_huff.ready) return;
    for (size_t s = 0; s < 257; s++) s_huff.count[k_huff_lens[s]]++;
    uint16_t total = 0;
    uint32_t code = 0;
    for (int l = 1; l <= 30; l++) {
        code <<= 1;
        s_huff.first[l] = code;
        s_huff.off[l] = total;
        total += s_huff.count[l];
        code += s_huff.count[l];
    }
    uint16_t pos[31];
    memcpy(pos, s_huff.off, sizeof pos);
    for (uint16_t s = 0; s < 257; s++) {
        uint8_t l = k_huff_lens[s];
        s_huff.syms[pos[l]++] = s;
    }
    s_huff.ready = true;
}

static int huff_decode(const uint8_t *src, size_t slen, size_t max_out, char **out, size_t *out_len) {
    huff_init();
    size_t cap = slen < 16 ? 32 : slen * 2;
    if (cap > max_out) cap = max_out;
    char *buf = malloc(cap + 1);
    if (!buf) return WTLS_ERR_NOMEM;
    size_t len = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < slen; i++) {
        uint8_t byte = src[i];
        for (int b = 7; b >= 0; b--) {
            acc = (acc << 1) | ((byte >> b) & 1u);
            bits++;
            if (bits < 5) continue;
            if (bits <= 30 && s_huff.count[bits] &&
                acc >= s_huff.first[bits] && acc - s_huff.first[bits] < s_huff.count[bits]) {
                uint16_t sym = s_huff.syms[s_huff.off[bits] + (acc - s_huff.first[bits])];
                if (sym == 256) { /* EOS inside string */
                    free(buf);
                    return WTLS_ERR_HTTP;
                }
                if (len >= max_out) {
                    free(buf);
                    return WTLS_ERR_HTTP_LIMIT;
                }
                if (len == cap) {
                    size_t ncap = cap <= max_out / 2 ? cap * 2 : max_out;
                    if (ncap == cap) ncap = max_out;
                    char *nb = realloc(buf, ncap + 1);
                    if (!nb) {
                        free(buf);
                        return WTLS_ERR_NOMEM;
                    }
                    buf = nb;
                    cap = ncap;
                }
                buf[len++] = (char)(uint8_t)sym;
                acc = 0;
                bits = 0;
            }
        }
    }
    if (bits >= 8 || (bits > 0 && acc != ((1u << bits) - 1u))) {
        free(buf);
        return WTLS_ERR_HTTP;
    }
    buf[len] = 0;
    *out = buf;
    *out_len = len;
    return WTLS_OK;
}

/* ---- Integers & Strings (RFC 7541 §5.1 / §5.2) ---------------------------- */

int wtls_hpack_encode_int(uint8_t *dst, size_t cap, size_t *off, uint8_t prefix_mask,
                          unsigned prefix_bits, uint64_t val) {
    if (!dst || !off || prefix_bits < 1 || prefix_bits > 8 || *off >= cap) return WTLS_ERR_ARG;
    uint8_t max_prefix = (uint8_t)((1u << prefix_bits) - 1u);
    if (val < max_prefix) {
        dst[(*off)++] = (uint8_t)((prefix_mask & (uint8_t)~max_prefix) | (uint8_t)val);
        return WTLS_OK;
    }
    dst[(*off)++] = (uint8_t)((prefix_mask & (uint8_t)~max_prefix) | max_prefix);
    val -= max_prefix;
    while (val >= 128u) {
        if (*off >= cap) return WTLS_ERR_HTTP_LIMIT;
        dst[(*off)++] = (uint8_t)((val & 0x7fu) | 0x80u);
        val >>= 7;
    }
    if (*off >= cap) return WTLS_ERR_HTTP_LIMIT;
    dst[(*off)++] = (uint8_t)val;
    return WTLS_OK;
}

int wtls_hpack_decode_int(const uint8_t **p, const uint8_t *end, unsigned prefix_bits, uint64_t *out) {
    if (!p || !*p || !end || *p >= end || prefix_bits < 1 || prefix_bits > 8 || !out) return WTLS_ERR_HTTP;
    const uint8_t *cur = *p;
    uint64_t max_prefix = (1u << prefix_bits) - 1u;
    uint64_t val = (*cur++) & max_prefix;
    if (val < max_prefix) {
        *p = cur;
        *out = val;
        return WTLS_OK;
    }
    unsigned m = 0;
    for (;;) {
        if (cur >= end || m > 56) return WTLS_ERR_HTTP;
        uint8_t b = *cur++;
        uint64_t add = (uint64_t)(b & 0x7fu) << m;
        if (val > UINT64_MAX - add) return WTLS_ERR_HTTP;
        val += add;
        m += 7;
        if ((b & 0x80u) == 0) break;
    }
    *p = cur;
    *out = val;
    return WTLS_OK;
}

int wtls_hpack_decode_str(const uint8_t **p, const uint8_t *end, size_t max_out,
                          char **out, size_t *out_len) {
    if (!p || !*p || !end || *p >= end || !out || !out_len) return WTLS_ERR_HTTP;
    *out = NULL;
    *out_len = 0;
    bool huff = ((**p) & 0x80u) != 0;
    uint64_t slen = 0;
    int st = wtls_hpack_decode_int(p, end, 7, &slen);
    if (st != WTLS_OK) return st;
    if (slen > (uint64_t)(end - *p)) return WTLS_ERR_HTTP;
    const uint8_t *raw = *p;
    *p += (size_t)slen;
    if (huff) return huff_decode(raw, (size_t)slen, max_out, out, out_len);
    if (slen > max_out) return WTLS_ERR_HTTP_LIMIT;
    char *s = malloc((size_t)slen + 1);
    if (!s) return WTLS_ERR_NOMEM;
    if (slen) memcpy(s, raw, (size_t)slen);
    s[slen] = 0;
    *out = s;
    *out_len = (size_t)slen;
    return WTLS_OK;
}

/* ---- Dynamic Table (RFC 7541 §4) ------------------------------------------ */

void wtls_hpack_init(wtls_hpack_ctx *ctx, size_t max_allowed) {
    if (!ctx) return;
    memset(ctx, 0, sizeof *ctx);
    if (max_allowed > WTLS_HPACK_DEFAULT_TABLE) max_allowed = WTLS_HPACK_DEFAULT_TABLE;
    ctx->max_allowed = max_allowed;
    ctx->max_size = max_allowed;
}

void wtls_hpack_free(wtls_hpack_ctx *ctx) {
    if (!ctx) return;
    for (size_t i = 0; i < ctx->count; i++) {
        free(ctx->entries[i].name);
        free(ctx->entries[i].value);
    }
    free(ctx->entries);
    memset(ctx, 0, sizeof *ctx);
}

static void evict_to(wtls_hpack_ctx *ctx, size_t limit) {
    while (ctx->count > 0 && ctx->size > limit) {
        size_t last = ctx->count - 1;
        size_t cost = ctx->entries[last].name_len + ctx->entries[last].value_len + 32u;
        free(ctx->entries[last].name);
        free(ctx->entries[last].value);
        ctx->count--;
        ctx->size -= cost;
    }
}

static int dyn_insert(wtls_hpack_ctx *ctx, const char *name, size_t name_len,
                      const char *value, size_t value_len) {
    if (name_len > SIZE_MAX - value_len - 32u) return WTLS_ERR_HTTP_LIMIT;
    size_t cost = name_len + value_len + 32u;
    if (cost > ctx->max_size) {
        evict_to(ctx, 0);
        return WTLS_OK;
    }
    evict_to(ctx, ctx->max_size - cost);
    if (ctx->count == ctx->cap) {
        size_t ncap = ctx->cap ? ctx->cap * 2 : 16u;
        wtls_hpack_entry *ne = realloc(ctx->entries, ncap * sizeof *ne);
        if (!ne) return WTLS_ERR_NOMEM;
        ctx->entries = ne;
        ctx->cap = ncap;
    }
    char *nc = malloc(name_len + 1);
    char *vc = malloc(value_len + 1);
    if (!nc || !vc) {
        free(nc);
        free(vc);
        return WTLS_ERR_NOMEM;
    }
    memcpy(nc, name, name_len);
    nc[name_len] = 0;
    memcpy(vc, value, value_len);
    vc[value_len] = 0;
    if (ctx->count > 0)
        memmove(&ctx->entries[1], &ctx->entries[0], ctx->count * sizeof ctx->entries[0]);
    ctx->entries[0].name = nc;
    ctx->entries[0].name_len = name_len;
    ctx->entries[0].value = vc;
    ctx->entries[0].value_len = value_len;
    ctx->count++;
    ctx->size += cost;
    return WTLS_OK;
}

static int table_lookup(const wtls_hpack_ctx *ctx, uint64_t idx, const char **name, size_t *name_len,
                        const char **value, size_t *value_len) {
    if (idx == 0) return WTLS_ERR_HTTP;
    if (idx <= HPACK_STATIC_COUNT) {
        const hpack_static_entry *e = &k_static[idx - 1];
        *name = e->name;
        *name_len = strlen(e->name);
        *value = e->value;
        *value_len = strlen(e->value);
        return WTLS_OK;
    }
    uint64_t dyn_idx = idx - (HPACK_STATIC_COUNT + 1u);
    if (dyn_idx >= ctx->count) return WTLS_ERR_HTTP;
    const wtls_hpack_entry *d = &ctx->entries[dyn_idx];
    *name = d->name;
    *name_len = d->name_len;
    *value = d->value;
    *value_len = d->value_len;
    return WTLS_OK;
}

/* ---- Encoder -------------------------------------------------------------- */

typedef struct {
    uint8_t *p;
    size_t len;
    size_t cap;
    int err;
} hpack_buf;

static void hbuf_ensure(hpack_buf *b, size_t extra) {
    if (b->err) return;
    if (extra > WTLS_HTTP_MAX_HEAD - b->len) {
        b->err = WTLS_ERR_ARG;
        return;
    }
    while (b->len + extra > b->cap) {
        size_t ncap = b->cap <= WTLS_HTTP_MAX_HEAD / 2 ? b->cap * 2 : WTLS_HTTP_MAX_HEAD;
        if (ncap < b->len + extra) ncap = WTLS_HTTP_MAX_HEAD;
        uint8_t *np = realloc(b->p, ncap);
        if (!np) {
            b->err = WTLS_ERR_NOMEM;
            return;
        }
        b->p = np;
        b->cap = ncap;
    }
}

static void hbuf_int(hpack_buf *b, uint8_t mask, unsigned prefix, uint64_t val) {
    hbuf_ensure(b, 12);
    if (b->err) return;
    int st = wtls_hpack_encode_int(b->p, b->cap, &b->len, mask, prefix, val);
    if (st != WTLS_OK) b->err = st;
}

static void hbuf_str(hpack_buf *b, const char *s, size_t len, bool lowercase) {
    hbuf_int(b, 0x00, 7, (uint64_t)len);
    hbuf_ensure(b, len);
    if (b->err) return;
    if (lowercase) {
        for (size_t i = 0; i < len; i++) b->p[b->len + i] = (uint8_t)tolower((unsigned char)s[i]);
    } else if (len) {
        memcpy(b->p + b->len, s, len);
    }
    b->len += len;
}

static size_t find_static_name(const char *name) {
    for (size_t i = 0; i < HPACK_STATIC_COUNT; i++) {
        if (wtls_http_ieq(name, k_static[i].name)) return i + 1u;
    }
    return 0;
}

static bool is_sensitive_header(const char *name) {
    return wtls_http_ieq(name, "authorization") || wtls_http_ieq(name, "cookie") ||
           wtls_http_ieq(name, "proxy-authorization");
}

static void emit_literal(hpack_buf *b, const char *name, const char *val, bool lower_name, bool never_idx) {
    uint8_t mask = never_idx ? 0x10u : 0x00u;
    size_t idx = find_static_name(name);
    if (idx) {
        hbuf_int(b, mask, 4, (uint64_t)idx);
    } else {
        hbuf_int(b, mask, 4, 0);
        hbuf_str(b, name, strlen(name), lower_name);
    }
    hbuf_str(b, val, strlen(val), false);
}

static bool value_ok_req(const char *v) {
    for (; *v; v++) {
        unsigned char c = (unsigned char)*v;
        if ((c < 0x20 && c != '\t') || c == 0x7f) return false;
    }
    return true;
}

int wtls_hpack_encode_request(const char *method, const wtls_url *u, const wtls_http_header *h,
                              size_t n, bool has_content_length, size_t body_len,
                              bool want_gzip, bool expect_100, uint8_t **out, size_t *out_len) {
    static const char *const forbidden[] = {
        "host", "content-length", "transfer-encoding", "connection",
        "keep-alive", "proxy-connection", "upgrade"
    };
    if (!u || !out || !out_len || (n && !h)) return WTLS_ERR_ARG;
    *out = NULL;
    *out_len = 0;
    if (!method) method = "GET";
    if (!wtls_http_is_token(method) || n > WTLS_HTTP_MAX_REQ_HEADERS) return WTLS_ERR_ARG;
    for (size_t i = 0; i < n; i++) {
        if (!wtls_http_is_token(h[i].name) || !h[i].value || !value_ok_req(h[i].value))
            return WTLS_ERR_ARG;
        for (size_t k = 0; k < sizeof forbidden / sizeof forbidden[0]; k++)
            if (wtls_http_ieq(h[i].name, forbidden[k])) return WTLS_ERR_ARG;
        if (wtls_http_ieq(h[i].name, "te") && !wtls_http_ieq(h[i].value, "trailers"))
            return WTLS_ERR_ARG;
    }

    hpack_buf b = {malloc(512), 0, 512, WTLS_OK};
    if (!b.p) return WTLS_ERR_NOMEM;

    /* 1. :method */
    if (strcmp(method, "GET") == 0) hbuf_int(&b, 0x80u, 7, 2);
    else if (strcmp(method, "POST") == 0) hbuf_int(&b, 0x80u, 7, 3);
    else emit_literal(&b, ":method", method, false, false);

    /* 2. :scheme */
    hbuf_int(&b, 0x80u, 7, u->secure ? 7u : 6u);

    /* 3. :authority */
    char auth[270];
    if (u->port != (u->secure ? 443 : 80))
        snprintf(auth, sizeof auth, "%s:%u", u->host, (unsigned)u->port);
    else
        snprintf(auth, sizeof auth, "%s", u->host);
    emit_literal(&b, ":authority", auth, false, false);

    /* 4. :path */
    if (u->target_needs_slash && u->target_len == 0) {
        hbuf_int(&b, 0x80u, 7, 4);
    } else if (!u->target_needs_slash && u->target_len == 1 && u->target[0] == '/') {
        hbuf_int(&b, 0x80u, 7, 4);
    } else if (!u->target_needs_slash && u->target_len == 11 && memcmp(u->target, "/index.html", 11) == 0) {
        hbuf_int(&b, 0x80u, 7, 5);
    } else {
        size_t plen = u->target_len + (u->target_needs_slash ? 1u : 0u);
        hbuf_int(&b, 0x00u, 4, 4);
        hbuf_int(&b, 0x00u, 7, (uint64_t)plen);
        hbuf_ensure(&b, plen);
        if (b.err == WTLS_OK) {
            if (u->target_needs_slash) b.p[b.len++] = '/';
            if (u->target_len) {
                memcpy(b.p + b.len, u->target, u->target_len);
                b.len += u->target_len;
            }
        }
    }

    /* Regular headers (all lowercase field names required by RFC 9113 §8.2) */
    if (!wtls_http_headers_have(h, n, "user-agent"))
        emit_literal(&b, "user-agent", "libwiitls/" WTLS_VERSION, false, false);
    if (!wtls_http_headers_have(h, n, "accept"))
        emit_literal(&b, "accept", "*/*", false, false);
    if (want_gzip && !wtls_http_headers_have(h, n, "accept-encoding"))
        hbuf_int(&b, 0x80u, 7, 16); /* static 16: accept-encoding: gzip, deflate */
    if (has_content_length) {
        char num[32];
        snprintf(num, sizeof num, "%llu", (unsigned long long)body_len);
        emit_literal(&b, "content-length", num, false, false);
    }
    if (expect_100 && !wtls_http_headers_have(h, n, "expect"))
        emit_literal(&b, "expect", "100-continue", false, false);
    for (size_t i = 0; i < n; i++)
        emit_literal(&b, h[i].name, h[i].value, true, is_sensitive_header(h[i].name));

    if (b.err != WTLS_OK) {
        free(b.p);
        return b.err;
    }
    *out = b.p;
    *out_len = b.len;
    return WTLS_OK;
}

/* ---- Decoder (RFC 7541 §6 + RFC 9113 §8.2) -------------------------------- */

static bool valid_resp_name(const char *s, size_t len) {
    if (!len || s[0] == ':') return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 'A' && c <= 'Z') return false; /* uppercase forbidden in h2 */
        if (!isalnum(c) && strchr("!#$%&'*+-.^_`|~", c) == NULL) return false;
    }
    return true;
}

static bool valid_resp_value(const char *s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0 || c == '\r' || c == '\n' || c == 0x7f) return false;
        if (c < 0x20 && c != '\t') return false;
    }
    return true;
}

static bool forbidden_resp_header(const char *name) {
    return strcmp(name, "connection") == 0 || strcmp(name, "keep-alive") == 0 ||
           strcmp(name, "proxy-connection") == 0 || strcmp(name, "transfer-encoding") == 0 ||
           strcmp(name, "upgrade") == 0;
}

static int parse_status(const char *val, size_t len) {
    if (len != 3) return -1;
    int s = 0;
    for (int i = 0; i < 3; i++) {
        if (val[i] < '0' || val[i] > '9') return -1;
        s = s * 10 + (val[i] - '0');
    }
    return (s >= 100 && s <= 599) ? s : -1;
}

typedef struct {
    char *p;
    size_t len;
    size_t cap;
} arena_buf;

static int arena_push(arena_buf *a, const char *s, size_t n, size_t *off_out) {
    if (n + 1u > WTLS_HTTP_MAX_HEAD - a->len) return WTLS_ERR_HTTP_LIMIT;
    while (a->len + n + 1u > a->cap) {
        size_t ncap = a->cap ? a->cap * 2 : 512u;
        if (ncap < a->len + n + 1u) ncap = a->len + n + 1u;
        if (ncap > WTLS_HTTP_MAX_HEAD) ncap = WTLS_HTTP_MAX_HEAD;
        char *np = realloc(a->p, ncap);
        if (!np) return WTLS_ERR_NOMEM;
        a->p = np;
        a->cap = ncap;
    }
    *off_out = a->len;
    if (n) memcpy(a->p + a->len, s, n);
    a->p[a->len + n] = 0;
    a->len += n + 1u;
    return WTLS_OK;
}

typedef struct {
    size_t name_off;
    size_t val_off;
} field_off;

int wtls_hpack_decode_head(wtls_hpack_ctx *ctx, const uint8_t *block, size_t block_len,
                           wtls_http_head *head, char **arena_out) {
    if (!ctx || !head || !arena_out || (!block && block_len)) return WTLS_ERR_ARG;
    memset(head, 0, sizeof *head);
    *arena_out = NULL;

    arena_buf arena = {NULL, 0, 0};
    field_off offs[WTLS_HTTP_MAX_FIELDS];
    size_t n_fields = 0;
    int status = -1;
    bool saw_regular = false;

    const uint8_t *p = block;
    const uint8_t *end = block + block_len;
    bool allow_size_update = true;
    int st = WTLS_OK;

    while (p < end) {
        uint8_t b = *p;
        if ((b & 0xe0u) == 0x20u) { /* Dynamic Table Size Update */
            if (!allow_size_update) {
                st = WTLS_ERR_HTTP;
                break;
            }
            uint64_t new_max = 0;
            st = wtls_hpack_decode_int(&p, end, 5, &new_max);
            if (st != WTLS_OK) break;
            if (new_max > ctx->max_allowed) {
                st = WTLS_ERR_HTTP;
                break;
            }
            ctx->max_size = (size_t)new_max;
            evict_to(ctx, ctx->max_size);
            continue;
        }
        allow_size_update = false;

        const char *name = NULL;
        const char *val = NULL;
        size_t name_len = 0, val_len = 0;
        char *owned_name = NULL;
        char *owned_val = NULL;
        bool add_dyn = false;

        if (b & 0x80u) {
            uint64_t idx = 0;
            st = wtls_hpack_decode_int(&p, end, 7, &idx);
            if (st == WTLS_OK) st = table_lookup(ctx, idx, &name, &name_len, &val, &val_len);
        } else {
            unsigned prefix;
            if (b & 0x40u) {
                prefix = 6;
                add_dyn = true;
            } else {
                prefix = 4; /* handles both 0000xxxx (without indexing) and 0001xxxx (never indexed) */
            }
            uint64_t name_idx = 0;
            st = wtls_hpack_decode_int(&p, end, prefix, &name_idx);
            if (st == WTLS_OK) {
                if (name_idx > 0) {
                    const char *dummy_v = NULL;
                    size_t dummy_vl = 0;
                    st = table_lookup(ctx, name_idx, &name, &name_len, &dummy_v, &dummy_vl);
                } else {
                    st = wtls_hpack_decode_str(&p, end, WTLS_HTTP_MAX_HEAD, &owned_name, &name_len);
                    name = owned_name;
                }
            }
            if (st == WTLS_OK) {
                st = wtls_hpack_decode_str(&p, end, WTLS_HTTP_MAX_HEAD, &owned_val, &val_len);
                val = owned_val;
            }
        }

        if (st == WTLS_OK && !valid_resp_value(val, val_len)) st = WTLS_ERR_HTTP;
        if (st == WTLS_OK) {
            if (name_len > 0 && name[0] == ':') {
                if (saw_regular || strcmp(name, ":status") != 0 || status != -1) {
                    st = WTLS_ERR_HTTP;
                } else {
                    status = parse_status(val, val_len);
                    if (status < 0) st = WTLS_ERR_HTTP;
                }
            } else {
                saw_regular = true;
                if (!valid_resp_name(name, name_len) || forbidden_resp_header(name)) {
                    st = WTLS_ERR_HTTP;
                } else if (n_fields >= WTLS_HTTP_MAX_FIELDS) {
                    st = WTLS_ERR_HTTP_LIMIT;
                } else {
                    st = arena_push(&arena, name, name_len, &offs[n_fields].name_off);
                    if (st == WTLS_OK)
                        st = arena_push(&arena, val, val_len, &offs[n_fields].val_off);
                    if (st == WTLS_OK) n_fields++;
                }
            }
        }
        if (st == WTLS_OK && add_dyn) st = dyn_insert(ctx, name, name_len, val, val_len);
        free(owned_name);
        free(owned_val);
        if (st != WTLS_OK) break;
    }

    if (st == WTLS_OK && status < 0) st = WTLS_ERR_HTTP;
    if (st != WTLS_OK) {
        free(arena.p);
        return st;
    }
    head->status = status;
    head->minor = 1; /* persistent framing compatibility */
    head->n_fields = n_fields;
    for (size_t i = 0; i < n_fields; i++) {
        head->fields[i].name = arena.p + offs[i].name_off;
        head->fields[i].value = arena.p + offs[i].val_off;
    }
    *arena_out = arena.p;
    return WTLS_OK;
}
