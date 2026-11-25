/* URL parsing and reference resolution (RFC 3986) for the HTTP client. */
#include "http/wtls_url.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static bool ieq_prefix(const char *s, const char *prefix) {
    for (; *prefix; s++, prefix++)
        if (tolower((unsigned char)*s) != *prefix) return false;
    return true;
}

static bool host_char(unsigned char c) {
    return isalnum(c) || c == '.' || c == '-';
}

int wtls_url_parse(const char *url, wtls_url *u) {
    if (!url || !u) return WTLS_ERR_ARG;
    if (strlen(url) > WTLS_HTTP_MAX_URL) return WTLS_ERR_ARG;
    memset(u, 0, sizeof *u);
    const char *p;
    if (ieq_prefix(url, "https://")) {
        u->secure = true;
        u->port = 443;
        p = url + 8;
    } else if (ieq_prefix(url, "http://")) {
        u->port = 80;
        p = url + 7;
    } else {
        return WTLS_ERR_ARG;
    }

    const char *h = p;
    while (host_char((unsigned char)*p)) p++;
    size_t hn = (size_t)(p - h);
    if (hn > 0 && h[hn - 1] == '.') hn--; /* one trailing dot */
    if (hn == 0 || hn > 253 || h[hn - 1] == '.') return WTLS_ERR_ARG;
    for (size_t i = 0; i < hn; i++) u->host[i] = (char)tolower((unsigned char)h[i]);
    u->host[hn] = 0;

    if (*p == ':') {
        p++;
        unsigned long port = 0;
        const char *d = p;
        while (*p >= '0' && *p <= '9') {
            port = port * 10 + (unsigned long)(*p - '0');
            if (port > 65535) return WTLS_ERR_ARG;
            p++;
        }
        if (p == d || port == 0) return WTLS_ERR_ARG;
        u->port = (uint16_t)port;
    }
    if (*p != 0 && *p != '/' && *p != '?' && *p != '#') return WTLS_ERR_ARG; /* '@', '[', ' ', ... */

    const char *t = p;
    for (const char *q = p; *q; q++) {
        unsigned char c = (unsigned char)*q;
        if (c < 0x21 || c > 0x7e) return WTLS_ERR_ARG;
    }
    while (*p && *p != '#') p++;
    u->target = t;
    u->target_len = (size_t)(p - t);
    u->target_needs_slash = (u->target_len == 0 || *t != '/');
    return WTLS_OK;
}

bool wtls_url_same_origin(const wtls_url *a, const wtls_url *b) {
    return a->secure == b->secure && a->port == b->port && strcmp(a->host, b->host) == 0;
}

/* ---- RFC 3986 §5.2 ---------------------------------------------------- */

typedef struct {
    const char *s;
    size_t n;
    bool defined;
} part;

typedef struct {
    part scheme, authority, path, query;
} parts;

/* Appendix B split; the fragment is dropped. */
static void split(const char *r, parts *o) {
    memset(o, 0, sizeof *o);
    const char *p = r;
    while (*p && *p != ':' && *p != '/' && *p != '?' && *p != '#') p++;
    if (*p == ':' && p != r && isalpha((unsigned char)r[0])) {
        o->scheme = (part){r, (size_t)(p - r), true};
        r = p + 1;
    }
    if (r[0] == '/' && r[1] == '/') {
        r += 2;
        p = r;
        while (*p && *p != '/' && *p != '?' && *p != '#') p++;
        o->authority = (part){r, (size_t)(p - r), true};
        r = p;
    }
    p = r;
    while (*p && *p != '?' && *p != '#') p++;
    o->path = (part){r, (size_t)(p - r), true};
    r = p;
    if (*r == '?') {
        r++;
        p = r;
        while (*p && *p != '#') p++;
        o->query = (part){r, (size_t)(p - r), true};
    }
}

/* remove_dot_segments (§5.2.4). in[0..n) is a writable scratch copy (rule
 * 2B/2C rewrite its first byte); returns the length written to out. */
static size_t remove_dots(char *in, size_t n, char *out) {
    size_t o = 0;
    while (n > 0) {
        if (n >= 3 && memcmp(in, "../", 3) == 0) { /* A */
            in += 3, n -= 3;
        } else if (n >= 2 && memcmp(in, "./", 2) == 0) {
            in += 2, n -= 2;
        } else if (n >= 3 && memcmp(in, "/./", 3) == 0) { /* B */
            in += 2, n -= 2;
        } else if (n == 2 && memcmp(in, "/.", 2) == 0) {
            in += 1, n -= 1;
            in[0] = '/';
        } else if (n >= 4 && memcmp(in, "/../", 4) == 0) { /* C */
            in += 3, n -= 3;
            while (o > 0 && out[o - 1] != '/') o--;
            if (o > 0) o--;
        } else if (n == 3 && memcmp(in, "/..", 3) == 0) {
            in += 2, n -= 2;
            in[0] = '/';
            while (o > 0 && out[o - 1] != '/') o--;
            if (o > 0) o--;
        } else if ((n == 1 && in[0] == '.') || (n == 2 && memcmp(in, "..", 2) == 0)) { /* D */
            n = 0;
        } else { /* E */
            size_t k = (in[0] == '/') ? 1 : 0;
            while (k < n && in[k] != '/') k++;
            memcpy(out + o, in, k);
            o += k, in += k, n -= k;
        }
    }
    return o;
}

/* a followed by b, through remove_dots, appended to buf at *len. */
static int put_path(char *buf, size_t *len, const char *a, size_t an, const char *b, size_t bn) {
    char *scratch = malloc(an + bn + 1);
    if (!scratch) return WTLS_ERR_NOMEM;
    memcpy(scratch, a, an);
    memcpy(scratch + an, b, bn);
    *len += remove_dots(scratch, an + bn, buf + *len);
    free(scratch);
    return WTLS_OK;
}

static void put(char *buf, size_t *len, const char *s, size_t n) {
    memcpy(buf + *len, s, n);
    *len += n;
}

int wtls_url_resolve(const char *base, const char *ref, char **out) {
    if (!base || !ref || !out) return WTLS_ERR_ARG;
    *out = NULL;
    parts B, R;
    split(base, &B);
    split(ref, &R);
    if (!B.scheme.defined || !B.authority.defined) return WTLS_ERR_ARG;

    char *buf = malloc(strlen(base) + strlen(ref) + 8);
    if (!buf) return WTLS_ERR_NOMEM;
    size_t len = 0;
    const part *scheme = R.scheme.defined ? &R.scheme : &B.scheme;
    for (size_t i = 0; i < scheme->n; i++) buf[len++] = (char)tolower((unsigned char)scheme->s[i]);
    put(buf, &len, "://", 3);

    const part *query;
    int rc = WTLS_OK;
    if (R.authority.defined) {
        put(buf, &len, R.authority.s, R.authority.n);
        query = &R.query;
        rc = put_path(buf, &len, "", 0, R.path.s, R.path.n);
    } else {
        /* A scheme without authority ("https:g") keeps the base authority:
         * it never yields a valid http(s) URL otherwise. */
        put(buf, &len, B.authority.s, B.authority.n);
        if (R.path.n == 0 && !R.scheme.defined) {
            put(buf, &len, B.path.s, B.path.n);
            query = R.query.defined ? &R.query : &B.query;
        } else {
            query = &R.query;
            if (R.scheme.defined || (R.path.n > 0 && R.path.s[0] == '/')) {
                rc = put_path(buf, &len, "", 0, R.path.s, R.path.n);
            } else if (B.path.n == 0) {
                rc = put_path(buf, &len, "/", 1, R.path.s, R.path.n);
            } else {
                size_t keep = B.path.n;
                while (keep > 0 && B.path.s[keep - 1] != '/') keep--;
                rc = put_path(buf, &len, B.path.s, keep, R.path.s, R.path.n);
            }
        }
    }
    if (rc != WTLS_OK) {
        free(buf);
        return rc;
    }
    if (query->defined) {
        buf[len++] = '?';
        put(buf, &len, query->s, query->n);
    }
    buf[len] = 0;
    *out = buf;
    return WTLS_OK;
}
