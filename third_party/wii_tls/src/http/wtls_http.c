/* HTTP/1.1 client: request, response head, body streaming (RFC 9112),
 * keep-alive pool reuse with a liveness check and one idempotent retry,
 * redirects, gzip Content-Encoding (WTLS_GZIP), over TLS (https) or plain
 * TCP (http).
 *
 * The response keeps the connection and one buffer: the head (parsed in
 * place, so field names and values point into it) followed by the body bytes
 * that arrived together with the head; wtls_http_read() serves those first. */
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <wiitls/http.h>

#include "http/wtls_http_chunked.h"
#if WTLS_GZIP
#include "http/wtls_http_gzip.h"
#endif
#include "http/wtls_http_parse.h"
#include "http/wtls_http_pool.h"
#include "http/wtls_http_req.h"
#include "http/wtls_url.h"
#include "plat/plat.h"
#include "wtls_dns.h"
#include "wtls_internal.h"

#define HEAD_BUF_START 2048u
#define SINGLE_WRITE_MAX 16384u

/* wtls_conn_info.alpn points into this list, so it must outlive every connection. */
static const char *const k_alpn[] = {"http/1.1", NULL};

/* A transport: a TLS connection (https) or a plain TCP socket (http). */
typedef struct {
    wtls_conn *tls;         /* NULL: plain */
    int fd;                 /* plain only */
    uint32_t io_timeout_ms; /* plain only; TLS keeps its own */
} http_tr;

struct wtls_http_response {
    http_tr *conn;
    char *buf;           /* head, then extra body bytes */
    size_t extra_off;    /* first unread extra byte */
    size_t extra_end;    /* end of the extra bytes */
    wtls_http_head head; /* points into buf */
    wtls_http_framing fr;
    uint64_t remaining;  /* WTLS_BODY_LENGTH */
    wtls_chunked chunked;
    int err;             /* sticky read error */
    bool eof;            /* body read to its end */
    bool head_request;
    char *url;
    uint8_t redirects;
    bool reused;
    bool secure;
    bool insecure;       /* insecure_skip_verify: part of the pool key */
    uint16_t port;
    char host[256];
    wtls_conn_info info;
#if WTLS_GZIP
    wtls_gzip *gz;       /* decoding Content-Encoding: gzip */
    uint8_t *gz_in;      /* raw body bytes waiting for the decoder */
    size_t gz_off, gz_end;
    bool gz_raw_end;     /* the raw body has ended */
#endif
};

/* ---- transport ------------------------------------------------------------ */

#define DEF_CONNECT_MS 5000u
#define DEF_IO_MS 15000u

/* Plain TCP read: bytes, WTLS_ERR_CLOSED at the end of the stream (plain HTTP
 * has no close_notify: a TCP end is the end), WTLS_ERR_TIMEOUT or WTLS_ERR_IO. */
static int plain_read(http_tr *t, void *buf, size_t len) {
    if (len > WTLS_PLAT_MAX_CHUNK) len = WTLS_PLAT_MAX_CHUNK;
    uint64_t deadline = wtls_plat_now_ms() + t->io_timeout_ms;
    for (;;) {
        int k = wtls_plat_recv(t->fd, buf, len);
        if (k > 0) return k;
        if (k == 0) return WTLS_ERR_CLOSED;
        if (k != WTLS_WANT_READ) return WTLS_ERR_IO;
        uint64_t now = wtls_plat_now_ms();
        if (now >= deadline) return WTLS_ERR_TIMEOUT;
        int p = wtls_plat_poll(t->fd, true, false, (uint32_t)(deadline - now));
        if (p < 0) return WTLS_ERR_IO;
        if (p == 0) return WTLS_ERR_TIMEOUT;
    }
}

static int plain_write(http_tr *t, const void *buf, size_t len) {
    if (len > WTLS_PLAT_MAX_CHUNK) len = WTLS_PLAT_MAX_CHUNK;
    uint64_t deadline = wtls_plat_now_ms() + t->io_timeout_ms;
    for (;;) {
        int k = wtls_plat_send(t->fd, buf, len);
        if (k > 0) return k;
        if (k != WTLS_WANT_WRITE) return WTLS_ERR_IO;
        uint64_t now = wtls_plat_now_ms();
        if (now >= deadline) return WTLS_ERR_TIMEOUT;
        int p = wtls_plat_poll(t->fd, false, true, (uint32_t)(deadline - now));
        if (p < 0) return WTLS_ERR_IO;
        if (p == 0) return WTLS_ERR_TIMEOUT;
    }
}

static int tr_read(wtls_http_response *r, void *buf, size_t len) {
    http_tr *t = r->conn;
    return t->tls ? wtls_read(t->tls, buf, len) : plain_read(t, buf, len);
}

static int tr_write_all(http_tr *t, const void *buf, size_t len) {
    const uint8_t *p = buf;
    while (len) {
        int k = t->tls ? wtls_write(t->tls, p, len) : plain_write(t, p, len);
        if (k <= 0) return k == WTLS_ERR_CLOSED || k == 0 ? WTLS_ERR_IO : k;
        p += k;
        len -= (size_t)k;
    }
    return WTLS_OK;
}

static uint32_t tr_timeout(const http_tr *t) { return t->tls ? t->tls->io_timeout_ms : t->io_timeout_ms; }

static void tr_set_timeout(http_tr *t, uint32_t ms) {
    if (t->tls) t->tls->io_timeout_ms = ms;
    else t->io_timeout_ms = ms;
}

void wtls_http_tr_close(void *c) {
    http_tr *t = c;
    if (!t) return;
    if (t->tls) wtls_close(t->tls);
    else wtls_plat_close(t->fd);
    free(t);
}

/* Liveness check of a pooled connection: alive if nothing is readable. A
 * readable plain socket is dead (data or EOF nobody asked for). A readable TLS
 * socket must answer one non-blocking 1-byte read with WTLS_WANT_READ (only
 * post-handshake messages such as a late TLS 1.3 ticket arrived); data,
 * close_notify, EOF or an error mean the connection is dead. */
static bool tr_alive(http_tr *t) {
    if (t->tls && wtls_pending(t->tls)) return false;
    int p = wtls_plat_poll(t->tls ? t->tls->fd : t->fd, true, false, 0);
    if (p == 0) return true;
    if (p < 0 || !t->tls) return false;
    uint8_t b;
    if (wtls_set_nonblocking(t->tls, true) != WTLS_OK) return false;
    int k = wtls_read(t->tls, &b, 1);
    wtls_set_nonblocking(t->tls, false);
    return k == WTLS_WANT_READ;
}

/* A new transport to host:port: TLS with opt, or plain TCP with opt's connect
 * and I/O timeouts (DNS cache as wtls_connect). */
static int tr_connect(http_tr **out, bool secure, const char *host, uint16_t port, const wtls_options *opt) {
    *out = NULL;
    if (!wtls_core_initialized()) return WTLS_ERR_STATE;
    http_tr *t = calloc(1, sizeof *t);
    if (!t) return WTLS_ERR_NOMEM;
    t->fd = -1;
    int st;
    if (secure) {
        st = wtls_connect(&t->tls, host, port, opt);
    } else {
        uint32_t ip;
        bool literal = wtls_plat_parse_ipv4(host, &ip);
        st = literal ? WTLS_OK : wtls_dns_lookup(host, &ip, wtls_plat_now_ms());
        if (st != WTLS_OK) st = WTLS_ERR_DNS;
        if (st == WTLS_OK) {
            st = wtls_plat_connect(ip, port, opt->connect_timeout_ms ? opt->connect_timeout_ms : DEF_CONNECT_MS,
                                   &t->fd);
            if (st != WTLS_OK) {
                t->fd = -1;
                if (!literal) wtls_dns_invalidate(host); /* the address may have moved */
            }
        }
        t->io_timeout_ms = opt->io_timeout_ms ? opt->io_timeout_ms : DEF_IO_MS;
    }
    if (st != WTLS_OK) {
        free(t);
        return st;
    }
    *out = t;
    return WTLS_OK;
}

/* ---- open ------------------------------------------------------------------- */

static void response_free(wtls_http_response *r) {
    if (!r) return;
    if (r->conn) wtls_http_tr_close(r->conn);
#if WTLS_GZIP
    wtls_gzip_free(r->gz);
    free(r->gz_in);
#endif
    free(r->buf);
    free(r->url);
    free(r);
}

static bool decoding(const wtls_http_response *r) {
#if WTLS_GZIP
    return r->gz != NULL;
#else
    (void)r;
    return false;
#endif
}

/* One request as it goes out on the current hop: a redirect may change the
 * method, drop the body and strip credential headers. */
typedef struct {
    const wtls_http_request *req;
    const char *method;
    const void *body; /* NULL: no body */
    size_t body_len;
    const wtls_http_header *headers;
    size_t n_headers;
    bool want_gzip; /* offer gzip and decode it */
    const char *head;
    size_t head_len;
    wtls_options opt;
} request_ctx;

static int send_request(http_tr *c, const request_ctx *x) {
    size_t body_len = x->body ? x->body_len : 0;
    if (body_len && x->head_len + body_len <= SINGLE_WRITE_MAX) {
        char *all = malloc(x->head_len + body_len);
        if (!all) return WTLS_ERR_NOMEM;
        memcpy(all, x->head, x->head_len);
        memcpy(all + x->head_len, x->body, body_len);
        int st = tr_write_all(c, all, x->head_len + body_len);
        free(all);
        return st;
    }
    int st = tr_write_all(c, x->head, x->head_len);
    if (st == WTLS_OK && body_len) st = tr_write_all(c, x->body, body_len);
    return st;
}

/* Reads until a final (non-1xx) response head is parsed. On success r->buf
 * holds it and r->extra_* the body bytes behind it. *got: a response byte arrived. */
static int read_head(wtls_http_response *r, uint32_t timeout_ms, bool *got) {
    size_t cap = HEAD_BUF_START, fill = 0;
    free(r->buf);
    char *buf = r->buf = malloc(cap);
    if (!buf) return WTLS_ERR_NOMEM;
    uint64_t deadline = wtls_plat_now_ms() + timeout_ms;
    for (;;) {
        size_t end = wtls_http_head_end(buf, fill);
        if (end) {
            int st = wtls_http_parse_head(buf, end, &r->head);
            if (st != WTLS_OK) return st;
            if (r->head.status >= 200) {
                r->extra_off = end;
                r->extra_end = fill;
                return WTLS_OK;
            }
            if (r->head.status == 101) return WTLS_ERR_HTTP; /* no upgrade was asked for */
            memmove(buf, buf + end, fill - end);           /* interim 1xx: skip it */
            fill -= end;
            continue;
        }
        if (fill == WTLS_HTTP_MAX_HEAD) return WTLS_ERR_HTTP_LIMIT;
        if (fill == cap) {
            size_t ncap = cap * 2 > WTLS_HTTP_MAX_HEAD ? WTLS_HTTP_MAX_HEAD : cap * 2;
            char *nb = realloc(buf, ncap);
            if (!nb) return WTLS_ERR_NOMEM;
            r->buf = buf = nb;
            cap = ncap;
        }
        uint64_t now = wtls_plat_now_ms();
        if (now >= deadline) return WTLS_ERR_TIMEOUT;
        tr_set_timeout(r->conn, (uint32_t)(deadline - now));
        int k = tr_read(r, buf + fill, cap - fill);
        if (k > 0) {
            fill += (size_t)k;
            *got = true;
        } else if (k == WTLS_ERR_CLOSED) {
            return WTLS_ERR_IO; /* closed before a complete head */
        } else {
            return k;
        }
    }
}

static int connect_fresh(wtls_http_response *r, request_ctx *x) {
    r->reused = false;
    int st = tr_connect(&r->conn, r->secure, r->host, r->port, &x->opt);
    if (st != WTLS_OK) r->conn = NULL;
    return st;
}

/* A live pooled connection for the key, else a new one. */
static int acquire(wtls_http_response *r, request_ctx *x) {
    void *c;
    while ((c = wtls_http_pool_take(r->secure, r->host, r->port, r->insecure, wtls_plat_now_ms()))) {
        if (tr_alive(c)) {
            r->conn = c;
            r->reused = true;
            /* this request's I/O timeout, not the one of the request that opened it */
            tr_set_timeout(c, x->opt.io_timeout_ms ? x->opt.io_timeout_ms : DEF_IO_MS);
            return WTLS_OK;
        }
        wtls_http_tr_close(c);
    }
    return connect_fresh(r, x);
}

/* Request out, final response head in. The connection's own I/O timeout is
 * restored afterwards (the head phase runs on the header deadline). */
static int exchange(wtls_http_response *r, const request_ctx *x, bool *got) {
    *got = false;
    if (r->conn->tls) wtls_get_info(r->conn->tls, &r->info);
    else memset(&r->info, 0, sizeof r->info);
    int st = send_request(r->conn, x);
    if (st != WTLS_OK) return st;
    uint32_t io_timeout = tr_timeout(r->conn);
    uint32_t t = x->req->header_timeout_ms ? x->req->header_timeout_ms : WTLS_HTTP_DEFAULT_HEADER_TIMEOUT_MS;
    st = read_head(r, t, got);
    tr_set_timeout(r->conn, io_timeout);
    return st;
}

/* Pools the connection if its response was read to its end, may carry another
 * request and holds no unread bytes; closes it otherwise. */
static void release_conn(wtls_http_response *r) {
    if (!r->conn) return;
    if (r->eof && !r->err && r->fr.reusable && r->extra_off == r->extra_end) {
        wtls_shutdown_hook = wtls_http_pool_flush;
        wtls_http_pool_put(r->conn, r->secure, r->host, r->port, r->insecure, wtls_plat_now_ms());
    } else {
        wtls_http_tr_close(r->conn);
    }
    r->conn = NULL;
}

/* Reads a redirect body away so the connection can be reused. A body of
 * unknown length or over WTLS_HTTP_REDIRECT_DRAIN_MAX stays unread, and
 * release_conn() then closes the connection. */
static void drain_body(wtls_http_response *r) {
    if (r->fr.kind == WTLS_BODY_CLOSE) return;
    if (r->fr.kind == WTLS_BODY_LENGTH && r->fr.length > WTLS_HTTP_REDIRECT_DRAIN_MAX) return;
    char tmp[1024];
    size_t total = 0;
    while (!r->eof && total <= WTLS_HTTP_REDIRECT_DRAIN_MAX) {
        int k = wtls_http_read(r, tmp, sizeof tmp);
        if (k <= 0) return;
        total += (size_t)k;
    }
}

/* One request/response exchange against *u: head built, connection acquired
 * (pooled or new), final head read and framed. */
static int hop(wtls_http_response *r, request_ctx *x, const wtls_url *u) {
    char *head = NULL;
    int st = wtls_http_build_head(x->method, u, x->headers, x->n_headers, x->body != NULL, x->body_len,
                                  x->req->no_keepalive, x->want_gzip, &head, &x->head_len);
    if (st != WTLS_OK) return st;
    x->head = head;
    r->secure = u->secure;
    r->port = u->port;
    memcpy(r->host, u->host, sizeof r->host);
    r->head_request = strcmp(x->method, "HEAD") == 0;
    r->err = 0;
    r->eof = false;
    r->extra_off = r->extra_end = 0;

    bool got = false;
    st = acquire(r, x);
    if (st == WTLS_OK) st = exchange(r, x, &got);
    /* The server closed a reused connection as the request went out: one
     * second attempt on a new connection, for idempotent methods only. */
    if (st == WTLS_ERR_IO && r->reused && !got && wtls_http_method_idempotent(x->method)) {
        wtls_http_tr_close(r->conn);
        r->conn = NULL;
        st = connect_fresh(r, x);
        if (st == WTLS_OK) st = exchange(r, x, &got);
    }
    free(head);
    x->head = NULL;
    if (st == WTLS_OK) st = wtls_http_frame(&r->head, r->head_request, !x->req->no_keepalive, &r->fr);
    if (st != WTLS_OK) return st;
    r->remaining = r->fr.length;
    r->eof = r->fr.kind == WTLS_BODY_NONE || (r->fr.kind == WTLS_BODY_LENGTH && r->fr.length == 0);
    wtls_chunked_init(&r->chunked);
    return WTLS_OK;
}

static bool is_redirect(int s) { return s == 301 || s == 302 || s == 303 || s == 307 || s == 308; }

static bool is_credential(const char *name) {
    return wtls_http_ieq(name, "Authorization") || wtls_http_ieq(name, "Proxy-Authorization") ||
           wtls_http_ieq(name, "Cookie");
}

/* Prepares the next hop for the redirect r holds: r->url, *u, the method and
 * body, and after the first cross-origin step the headers without
 * credentials (*filtered is that copy; the caller frees it). */
static int follow(wtls_http_response *r, request_ctx *x, wtls_url *u, const char *location,
                  wtls_http_header **filtered) {
    int limit = x->req->max_redirects ? x->req->max_redirects : WTLS_HTTP_DEFAULT_REDIRECTS;
    if (limit > UINT8_MAX) limit = UINT8_MAX;
    if (r->redirects >= limit) return WTLS_ERR_HTTP_REDIRECT;

    char *next = NULL;
    int st = wtls_url_resolve(r->url, location, &next);
    if (st == WTLS_ERR_NOMEM) return st;
    wtls_url nu;
    if (st != WTLS_OK || strlen(next) > WTLS_HTTP_MAX_URL || wtls_url_parse(next, &nu) != WTLS_OK ||
        (u->secure && !nu.secure)) {
        free(next);
        return WTLS_ERR_HTTP_REDIRECT; /* bad Location, or https -> http */
    }

    if (!*filtered && x->n_headers && !wtls_url_same_origin(u, &nu)) {
        wtls_http_header *h = malloc(x->n_headers * sizeof *h);
        if (!h) {
            free(next);
            return WTLS_ERR_NOMEM;
        }
        size_t n = 0;
        for (size_t i = 0; i < x->n_headers; i++)
            if (!is_credential(x->headers[i].name)) h[n++] = x->headers[i];
        *filtered = h;
        x->headers = h;
        x->n_headers = n;
    }

    int s = r->head.status;
    if ((s == 303 && !r->head_request) || ((s == 301 || s == 302) && strcmp(x->method, "POST") == 0)) {
        x->method = "GET";
        x->body = NULL;
        x->body_len = 0;
    }

    /* location points into r->buf, which the next hop replaces: use it first. */
    drain_body(r);
    release_conn(r);
    free(r->url);
    r->url = next;
    *u = nu; /* nu.target points into next, now r->url */
    r->redirects++;
    return WTLS_OK;
}

#define GZ_IN_BUF 16384u

/* Content-Encoding of the final response. Only when gzip was offered: gzip or
 * x-gzip is decoded, identity or no field is raw, anything else is an error.
 * Redirect bodies are drained raw, so only the final response gets here. */
static int setup_encoding(wtls_http_response *r, bool want_gzip) {
#if WTLS_GZIP
    if (!want_gzip || r->fr.kind == WTLS_BODY_NONE) return WTLS_OK;
    const char *ce = wtls_http_head_get(&r->head, "content-encoding");
    if (!ce || !*ce || wtls_http_ieq(ce, "identity")) return WTLS_OK;
    if (!wtls_http_ieq(ce, "gzip") && !wtls_http_ieq(ce, "x-gzip")) return WTLS_ERR_HTTP;
    r->gz = wtls_gzip_new();
    r->gz_in = malloc(GZ_IN_BUF);
    if (!r->gz || !r->gz_in) return WTLS_ERR_NOMEM; /* response_free() frees what was made */
#else
    (void)r;
    (void)want_gzip;
#endif
    return WTLS_OK;
}

wtls_status wtls_http_open(wtls_http_response **out, const wtls_http_request *req) {
    if (!out) return WTLS_ERR_ARG;
    *out = NULL;
    if (!req || !req->url) return WTLS_ERR_ARG;
    if (req->body_len && !req->body) return WTLS_ERR_ARG;

    wtls_http_response *r = calloc(1, sizeof *r);
    if (!r) return WTLS_ERR_NOMEM;
    size_t url_len = strnlen(req->url, WTLS_HTTP_MAX_URL + 1);
    if (url_len > WTLS_HTTP_MAX_URL) {
        free(r);
        return WTLS_ERR_ARG;
    }
    r->url = malloc(url_len + 1);
    if (!r->url) {
        free(r);
        return WTLS_ERR_NOMEM;
    }
    memcpy(r->url, req->url, url_len + 1);

    wtls_url u;
    int st = wtls_url_parse(r->url, &u);
    if (st != WTLS_OK) {
        response_free(r);
        return st;
    }
    request_ctx x = {.req = req,
                     .method = req->method ? req->method : "GET",
                     .body = req->body,
                     .body_len = req->body_len,
                     .headers = req->headers,
                     .n_headers = req->n_headers};
#if WTLS_GZIP
    /* An application that sets Accept-Encoding itself gets the body as sent. */
    x.want_gzip = !req->no_gzip && !wtls_http_headers_have(req->headers, req->n_headers, "Accept-Encoding");
#endif
    if (req->tls) x.opt = *req->tls;
    x.opt.alpn = k_alpn;
    r->insecure = x.opt.insecure_skip_verify;

    wtls_http_header *filtered = NULL;
    for (;;) {
        st = hop(r, &x, &u);
        if (st != WTLS_OK) break;
        const char *location = wtls_http_head_get(&r->head, "location");
        if (req->max_redirects < 0 || !is_redirect(r->head.status) || !location) break;
        st = follow(r, &x, &u, location, &filtered);
        if (st != WTLS_OK) break;
    }
    free(filtered);
    if (st == WTLS_OK) st = setup_encoding(r, x.want_gzip);
    if (st != WTLS_OK) {
        response_free(r);
        return st;
    }
    *out = r;
    return WTLS_OK;
}

/* ---- accessors ---------------------------------------------------------------- */

int wtls_http_status(const wtls_http_response *r) { return r ? r->head.status : 0; }

const char *wtls_http_get_header(const wtls_http_response *r, const char *name) {
    if (!r || !name) return NULL;
    return wtls_http_head_get(&r->head, name);
}

bool wtls_http_header_at(const wtls_http_response *r, size_t i, const char **name, const char **value) {
    if (!r || i >= r->head.n_fields) return false;
    if (name) *name = r->head.fields[i].name;
    if (value) *value = r->head.fields[i].value;
    return true;
}

/* Content-Length of a HEAD response: plain digits only, else -1. */
static int64_t head_content_length(const wtls_http_response *r) {
    const char *v = wtls_http_head_get(&r->head, "content-length");
    if (!v || !*v) return -1;
    int64_t n = 0;
    for (; *v; v++) {
        if (*v < '0' || *v > '9' || n > (INT64_MAX - 9) / 10) return -1;
        n = n * 10 + (*v - '0');
    }
    return n;
}

int64_t wtls_http_content_length(const wtls_http_response *r) {
    if (!r || decoding(r)) return -1; /* the decoded length is unknown */
    switch (r->fr.kind) {
    case WTLS_BODY_LENGTH:
        return r->fr.length > (uint64_t)INT64_MAX ? -1 : (int64_t)r->fr.length;
    case WTLS_BODY_NONE:
        return r->head_request ? head_content_length(r) : 0;
    default:
        return -1;
    }
}

wtls_status wtls_http_get_info(const wtls_http_response *r, wtls_http_info *out) {
    if (!r || !out) return WTLS_ERR_ARG;
    memset(out, 0, sizeof *out);
    out->url = r->url;
    out->redirects = r->redirects;
    out->reused_connection = r->reused;
    out->secure = r->secure;
    out->gzip = decoding(r);
    out->tls = r->info;
    return WTLS_OK;
}

/* ---- body ------------------------------------------------------------------------ */

/* Raw bytes: first the ones that came with the head, then the connection. */
static int pull(wtls_http_response *r, void *buf, size_t len) {
    if (r->extra_off < r->extra_end) {
        size_t n = r->extra_end - r->extra_off;
        if (n > len) n = len;
        memcpy(buf, r->buf + r->extra_off, n);
        r->extra_off += n;
        return (int)n;
    }
    return tr_read(r, buf, len);
}

static int fail(wtls_http_response *r, int err) {
    r->err = err;
    return err;
}

/* The body as framed on the wire (len >= 1, <= INT_MAX). */
static int raw_read(wtls_http_response *r, void *buf, size_t len) {
    if (r->eof) return 0;
    switch (r->fr.kind) {
    case WTLS_BODY_LENGTH: {
        if ((uint64_t)len > r->remaining) len = (size_t)r->remaining;
        int k = pull(r, buf, len);
        if (k < 0) return fail(r, k == WTLS_ERR_CLOSED ? WTLS_ERR_IO : k); /* truncated */
        r->remaining -= (uint64_t)k;
        if (!r->remaining) r->eof = true;
        return k;
    }
    case WTLS_BODY_CLOSE: {
        int k = pull(r, buf, len);
        if (k == WTLS_ERR_CLOSED) {
            r->eof = true;
            return 0;
        }
        return k < 0 ? fail(r, k) : k;
    }
    case WTLS_BODY_CHUNKED:
        for (;;) {
            int k = pull(r, buf, len);
            if (k < 0) return fail(r, k == WTLS_ERR_CLOSED ? WTLS_ERR_IO : k);
            size_t out = 0, rest = 0;
            int st = wtls_chunked_decode(&r->chunked, buf, (size_t)k, &out, &rest);
            if (st != WTLS_OK && st != WTLS_WANT_READ) return fail(r, st);
            if (st == WTLS_OK) {
                r->eof = true;
                if (rest) r->fr.reusable = false; /* bytes after the message: not ours to keep */
            }
            if (out) return (int)out;
            if (r->eof) return 0;
        }
    default:
        r->eof = true;
        return 0;
    }
}

#if WTLS_GZIP
/* The decoded body: raw bytes go through gz_in into the decoder. */
static int gz_read(wtls_http_response *r, uint8_t *buf, size_t len) {
    for (;;) {
        size_t used = 0, produced = 0;
        int st = wtls_gzip_inflate(r->gz, r->gz_in + r->gz_off, r->gz_end - r->gz_off, &used, buf, len, &produced);
        r->gz_off += used;
        if (st != WTLS_OK) return fail(r, st);
        if (produced) return (int)produced;
        /* All input taken and nothing came out: the decoder needs more. */
        if (r->gz_raw_end) {
            st = wtls_gzip_finish(r->gz); /* cut inside a member: WTLS_ERR_IO */
            return st != WTLS_OK ? fail(r, st) : 0;
        }
        int k = raw_read(r, r->gz_in, GZ_IN_BUF);
        if (k < 0) return k; /* raw_read made it sticky */
        if (k == 0) r->gz_raw_end = true;
        r->gz_off = 0;
        r->gz_end = (size_t)k;
    }
}
#endif

int wtls_http_read(wtls_http_response *r, void *buf, size_t len) {
    if (!r || (!buf && len)) return WTLS_ERR_ARG;
    if (r->err) return r->err;
    if (!len) return 0;
    if (len > INT_MAX) len = INT_MAX;
#if WTLS_GZIP
    if (r->gz) return gz_read(r, buf, len);
#endif
    return raw_read(r, buf, len);
}

/* ---- close ------------------------------------------------------------------------- */

void wtls_http_close(wtls_http_response *r) {
    if (!r) return;
    release_conn(r);
    response_free(r);
}

/* ---- fetch --------------------------------------------------------------------------- */

#define FETCH_BUF_START 16384u

wtls_status wtls_http_fetch(const wtls_http_request *req, size_t max_body, wtls_http_result *out) {
    if (!out) return WTLS_ERR_ARG;
    memset(out, 0, sizeof *out);
    if (!max_body) max_body = WTLS_HTTP_FETCH_DEFAULT_MAX;
    if (max_body > (size_t)INT_MAX - 1) max_body = (size_t)INT_MAX - 1;
    wtls_http_response *r;
    int st = wtls_http_open(&r, req);
    if (st != WTLS_OK) return st;
    out->status = r->head.status;

    size_t cap;
    if (r->fr.kind == WTLS_BODY_LENGTH && !decoding(r)) {
        if (r->fr.length > max_body) { /* refused before any body byte is read */
            wtls_http_close(r);
            return WTLS_ERR_HTTP_LIMIT;
        }
        cap = (size_t)r->fr.length;
    } else {
        cap = FETCH_BUF_START < max_body ? FETCH_BUF_START : max_body;
    }
    char *buf = malloc(cap + 1);
    size_t fill = 0;
    st = buf ? WTLS_OK : WTLS_ERR_NOMEM;
    while (st == WTLS_OK) {
        if (fill == cap) {
            /* Full: one more byte tells the end of the body from an overrun. */
            char b;
            int k = wtls_http_read(r, &b, 1);
            if (k == 0) break;
            if (k < 0) {
                st = k;
                break;
            }
            if (cap >= max_body) {
                st = WTLS_ERR_HTTP_LIMIT;
                break;
            }
            size_t ncap = cap * 2 < max_body ? cap * 2 : max_body;
            char *nb = realloc(buf, ncap + 1);
            if (!nb) {
                st = WTLS_ERR_NOMEM;
                break;
            }
            buf = nb;
            cap = ncap;
            buf[fill++] = b;
            continue;
        }
        int k = wtls_http_read(r, buf + fill, cap - fill);
        if (k == 0) break;
        if (k < 0) st = k;
        else fill += (size_t)k;
    }
    wtls_http_close(r);
    if (st != WTLS_OK) {
        free(buf);
        return st;
    }
    buf[fill] = '\0';
    out->body = buf;
    out->body_len = fill;
    return WTLS_OK;
}

void wtls_http_result_free(wtls_http_result *res) {
    if (!res) return;
    free(res->body);
    res->body = NULL;
    res->body_len = 0;
}
