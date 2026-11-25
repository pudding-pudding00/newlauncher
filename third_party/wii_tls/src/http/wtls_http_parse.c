/* HTTP/1.1 response heads and message framing (RFC 9112). */
#include "http/wtls_http_parse.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "http/wtls_http_req.h" /* wtls_http_is_token, wtls_http_ieq */

size_t wtls_http_head_end(const char *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (buf[i] != '\n') continue;
        if (i + 1 < len && buf[i + 1] == '\n') return i + 2;
        if (i + 2 < len && buf[i + 1] == '\r' && buf[i + 2] == '\n') return i + 3;
    }
    return 0;
}

static bool is_ows(char c) { return c == ' ' || c == '\t'; }

/* Field-value and reason-phrase bytes: HTAB, SP, VCHAR, obs-text. */
static bool text_ok(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c < 0x20 && c != '\t') || c == 0x7f) return false;
    }
    return true;
}

static bool status_line(const char *s, size_t n, wtls_http_head *out) {
    if (n < 12 || memcmp(s, "HTTP/1.", 7) != 0 || (s[7] != '0' && s[7] != '1') || s[8] != ' ') return false;
    for (int i = 9; i < 12; i++)
        if (!isdigit((unsigned char)s[i])) return false;
    if (n > 12 && s[12] != ' ') return false;
    int st = (s[9] - '0') * 100 + (s[10] - '0') * 10 + (s[11] - '0');
    if (st < 100 || st > 599) return false;
    if (n > 12 && !text_ok(s + 13, n - 13)) return false;
    out->status = st;
    out->minor = s[7] - '0';
    return true;
}

int wtls_http_parse_head(char *buf, size_t head_len, wtls_http_head *out) {
    memset(out, 0, sizeof *out);
    size_t pos = 0;
    char *last_end = NULL; /* end (NUL position) of the previous field's value, for obs-fold */
    bool first = true;
    while (pos < head_len) {
        size_t eol = pos;
        while (eol < head_len && buf[eol] != '\n') eol++;
        if (eol == head_len) return WTLS_ERR_HTTP; /* head without its empty line */
        size_t end = eol;
        if (end > pos && buf[end - 1] == '\r') end--;
        char *line = buf + pos;
        size_t n = end - pos;
        pos = eol + 1;

        if (first) {
            if (!status_line(line, n, out)) return WTLS_ERR_HTTP;
            first = false;
            continue;
        }
        if (n == 0) return pos == head_len ? WTLS_OK : WTLS_ERR_HTTP;

        if (is_ows(line[0])) { /* obs-fold: continue the previous value with one SP */
            if (!last_end) return WTLS_ERR_HTTP;
            size_t a = 0, b = n;
            while (a < b && is_ows(line[a])) a++;
            while (b > a && is_ows(line[b - 1])) b--;
            if (!text_ok(line + a, b - a)) return WTLS_ERR_HTTP;
            if (b > a) {
                *last_end = ' ';
                memmove(last_end + 1, line + a, b - a);
                last_end += 1 + (b - a);
            }
            *last_end = 0;
            continue;
        }

        size_t colon = 0;
        while (colon < n && line[colon] != ':') colon++;
        if (colon == n || colon == 0) return WTLS_ERR_HTTP;
        for (size_t i = 0; i < colon; i++) {
            unsigned char c = (unsigned char)line[i];
            line[i] = (char)tolower(c);
        }
        size_t a = colon + 1, b = n;
        while (a < b && is_ows(line[a])) a++;
        while (b > a && is_ows(line[b - 1])) b--;
        if (!text_ok(line + a, b - a)) return WTLS_ERR_HTTP;
        line[colon] = 0;
        if (!wtls_http_is_token(line)) return WTLS_ERR_HTTP; /* also rejects "Name :" */
        if (out->n_fields == WTLS_HTTP_MAX_FIELDS) return WTLS_ERR_HTTP_LIMIT;
        line[b] = 0; /* b <= n: overwrites OWS, CR or LF */
        out->fields[out->n_fields].name = line;
        out->fields[out->n_fields].value = line + a;
        out->n_fields++;
        last_end = line + b;
    }
    return WTLS_ERR_HTTP;
}

const char *wtls_http_head_get(const wtls_http_head *h, const char *name) {
    for (size_t i = 0; i < h->n_fields; i++)
        if (wtls_http_ieq(h->fields[i].name, name)) return h->fields[i].value;
    return NULL;
}

/* Calls fn on every comma-separated element (OWS trimmed, empty ones kept)
 * of every field named name; stops when fn returns false. */
typedef bool (*elem_fn)(const char *s, size_t n, void *ctx);

static bool each_elem(const wtls_http_head *h, const char *name, elem_fn fn, void *ctx) {
    for (size_t i = 0; i < h->n_fields; i++) {
        if (strcmp(h->fields[i].name, name) != 0) continue;
        const char *p = h->fields[i].value;
        for (;;) {
            const char *e = p;
            while (*e && *e != ',') e++;
            const char *a = p, *b = e;
            while (a < b && is_ows(*a)) a++;
            while (b > a && is_ows(b[-1])) b--;
            if (!fn(a, (size_t)(b - a), ctx)) return false;
            if (!*e) break;
            p = e + 1;
        }
    }
    return true;
}

typedef struct {
    bool seen, bad;
    uint64_t value;
} cl_state;

static bool cl_elem(const char *s, size_t n, void *ctx) {
    cl_state *st = ctx;
    uint64_t v = 0;
    if (n == 0) st->bad = true;
    for (size_t i = 0; i < n && !st->bad; i++) {
        if (s[i] < '0' || s[i] > '9' || v > (UINT64_MAX - 9) / 10) {
            st->bad = true;
            break;
        }
        v = v * 10 + (uint64_t)(s[i] - '0');
    }
    if (!st->bad && st->seen && v != st->value) st->bad = true;
    st->seen = true;
    st->value = v;
    return !st->bad;
}

typedef struct {
    size_t count;
    bool chunked_only;
} te_state;

static bool te_elem(const char *s, size_t n, void *ctx) {
    te_state *st = ctx;
    st->count++;
    st->chunked_only = st->count == 1 && n == 7 && strncasecmp(s, "chunked", 7) == 0;
    return true;
}

static bool close_elem(const char *s, size_t n, void *ctx) {
    if (n == 5 && strncasecmp(s, "close", 5) == 0) {
        *(bool *)ctx = true;
        return false;
    }
    return true;
}

int wtls_http_frame(const wtls_http_head *h, bool head_request, bool keepalive_requested,
                    wtls_http_framing *out) {
    memset(out, 0, sizeof *out);
    bool close = false;
    each_elem(h, "connection", close_elem, &close);
    out->reusable = keepalive_requested && h->minor == 1 && !close;

    if (head_request || (h->status >= 100 && h->status < 200) || h->status == 204 || h->status == 304) {
        out->kind = WTLS_BODY_NONE;
        return WTLS_OK;
    }
    te_state te = {0, false};
    each_elem(h, "transfer-encoding", te_elem, &te);
    cl_state cl = {false, false, 0};
    bool has_cl = wtls_http_head_get(h, "content-length") != NULL;
    if (te.count > 0) {
        if (!te.chunked_only) return WTLS_ERR_HTTP;
        out->kind = WTLS_BODY_CHUNKED;
        if (has_cl) out->reusable = false; /* RFC 9112 §6.3 (3): possible smuggling, do not reuse */
        return WTLS_OK;
    }
    if (has_cl) {
        if (!each_elem(h, "content-length", cl_elem, &cl) || cl.bad) return WTLS_ERR_HTTP;
        out->kind = WTLS_BODY_LENGTH;
        out->length = cl.value;
        return WTLS_OK;
    }
    out->kind = WTLS_BODY_CLOSE;
    out->reusable = false;
    return WTLS_OK;
}
