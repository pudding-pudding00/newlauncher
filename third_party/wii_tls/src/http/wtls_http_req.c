/* HTTP/1.1 request heads (RFC 9110 / 9112). */
#include "http/wtls_http_req.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool tchar(unsigned char c) {
    return isalnum(c) || (c && strchr("!#$%&'*+-.^_`|~", c) != NULL);
}

bool wtls_http_is_token(const char *s) {
    if (!s || !*s) return false;
    for (; *s; s++)
        if (!tchar((unsigned char)*s)) return false;
    return true;
}

bool wtls_http_ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    return *a == *b;
}

static bool value_ok(const char *v) {
    for (; *v; v++) {
        unsigned char c = (unsigned char)*v;
        if ((c < 0x20 && c != '\t') || c == 0x7f) return false;
    }
    return true;
}

bool wtls_http_headers_have(const wtls_http_header *h, size_t n, const char *name) {
    if (!h || !name) return false;
    for (size_t i = 0; i < n; i++)
        if (h[i].name && wtls_http_ieq(h[i].name, name)) return true;
    return false;
}

bool wtls_http_method_idempotent(const char *m) {
    static const char *const idem[] = {"GET", "HEAD", "PUT", "DELETE", "OPTIONS", "TRACE"};
    for (size_t i = 0; i < sizeof idem / sizeof idem[0]; i++)
        if (strcmp(m, idem[i]) == 0) return true;
    return false;
}

#define HEAD_REQ_START 512u

typedef struct {
    char *p;
    size_t len, cap;
    int err;
} sbuf;

static void add(sbuf *b, const char *s, size_t n) {
    if (b->err) return;
    if (n > WTLS_HTTP_MAX_HEAD - b->len) {
        b->err = WTLS_ERR_ARG;
        return;
    }
    while (b->len + n > b->cap) {
        size_t ncap = b->cap <= WTLS_HTTP_MAX_HEAD / 2 ? b->cap * 2 : WTLS_HTTP_MAX_HEAD;
        char *np = realloc(b->p, ncap + 1);
        if (!np) {
            b->err = WTLS_ERR_NOMEM;
            return;
        }
        b->p = np;
        b->cap = ncap;
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
}

static void adds(sbuf *b, const char *s) { add(b, s, strlen(s)); }

static void add_field(sbuf *b, const char *name, const char *value) {
    adds(b, name);
    add(b, ": ", 2);
    adds(b, value);
    add(b, "\r\n", 2);
}

int wtls_http_build_head(const char *method, const wtls_url *u, const wtls_http_header *h, size_t n,
                         bool has_body, size_t body_len, bool chunked, bool no_keepalive, bool want_gzip,
                         char **out, size_t *len) {
    static const char *const reserved[] = {"host", "content-length", "transfer-encoding", "connection"};
    if (!u || !out || !len || (n && !h)) return WTLS_ERR_ARG;
    *out = NULL;
    *len = 0;
    if (!method) method = "GET";
    if (!wtls_http_is_token(method) || n > WTLS_HTTP_MAX_REQ_HEADERS) return WTLS_ERR_ARG;
    for (size_t i = 0; i < n; i++) {
        if (!wtls_http_is_token(h[i].name) || !h[i].value || !value_ok(h[i].value)) return WTLS_ERR_ARG;
        for (size_t k = 0; k < sizeof reserved / sizeof reserved[0]; k++)
            if (wtls_http_ieq(h[i].name, reserved[k])) return WTLS_ERR_ARG;
    }

    sbuf b = {malloc(HEAD_REQ_START + 1), 0, HEAD_REQ_START, WTLS_OK};
    if (!b.p) return WTLS_ERR_NOMEM;
    char num[32];

    adds(&b, method);
    add(&b, " ", 1);
    if (u->target_needs_slash) add(&b, "/", 1);
    add(&b, u->target, u->target_len);
    adds(&b, " HTTP/1.1\r\nHost: ");
    adds(&b, u->host);
    if (u->port != (u->secure ? 443 : 80)) {
        snprintf(num, sizeof num, ":%u", (unsigned)u->port);
        adds(&b, num);
    }
    add(&b, "\r\n", 2);
    if (!wtls_http_headers_have(h, n, "User-Agent")) add_field(&b, "User-Agent", "libwiitls/" WTLS_VERSION);
    if (!wtls_http_headers_have(h, n, "Accept")) add_field(&b, "Accept", "*/*");
    if (want_gzip && !wtls_http_headers_have(h, n, "Accept-Encoding")) add_field(&b, "Accept-Encoding", "gzip");
    if (no_keepalive) add_field(&b, "Connection", "close");
    if (chunked) {
        add_field(&b, "Transfer-Encoding", "chunked");
    } else if (has_body || strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0 ||
               strcmp(method, "PATCH") == 0) {
        snprintf(num, sizeof num, "%llu", (unsigned long long)(has_body ? body_len : 0));
        add_field(&b, "Content-Length", num);
    }
    for (size_t i = 0; i < n; i++) add_field(&b, h[i].name, h[i].value);
    add(&b, "\r\n", 2);

    if (b.err != WTLS_OK) {
        free(b.p);
        return b.err;
    }
    b.p[b.len] = 0;
    *out = b.p;
    *len = b.len;
    return WTLS_OK;
}
