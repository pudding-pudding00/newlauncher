/* HTTP/1.1 client: request, response head, body streaming (RFC 9112),
 * keep-alive pool reuse with a liveness check and one idempotent retry,
 * redirects, gzip Content-Encoding (WTLS_GZIP), over TLS (https) or plain
 * TCP (http).
 *
 * The response keeps the connection and one buffer: the head (parsed in
 * place, so field names and values point into it) followed by the body bytes
 * that arrived together with the head; wtls_http_read() serves those first. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wiitls/http.h>

#include "http/wtls_http_chunked.h"
#if WTLS_GZIP
#include "http/wtls_http_gzip.h"
#endif
#if WTLS_HTTP2
#include "http/wtls_http2.h"
#endif
#include "http/wtls_http_parse.h"
#include "http/wtls_http_pool.h"
#include "http/wtls_http_req.h"
#include "http/wtls_url.h"
#include "plat/plat.h"
#include "wtls_cred.h"
#include "wtls_dns.h"
#include "wtls_internal.h"

#define HEAD_BUF_START 2048u
#define SINGLE_WRITE_MAX 16384u
#define UPLOAD_BUF_SIZE 16384u

/* wtls_conn_info.alpn points into these lists, so they must outlive every connection. */
static const char *const k_alpn_h1[] = {"http/1.1", NULL};
#if WTLS_HTTP2
static const char *const k_alpn_h2[] = {"h2", "http/1.1", NULL};
#endif

/* A transport: a TLS connection (https) or a plain TCP socket (http). */
typedef struct {
    wtls_conn *tls;         /* NULL: plain */
    int fd;                 /* plain only */
    uint32_t io_timeout_ms; /* plain only; TLS keeps its own */
    wtls_abort_cb abort_cb; /* plain only; TLS keeps its own */
    void *abort_ctx;
#if WTLS_HTTP2
    wtls_h2_conn *h2;
#endif
} http_tr;

struct wtls_http_response {
    http_tr *conn;
    char *buf;           /* head (HTTP/1.1) or HPACK string arena (HTTP/2) */
    size_t extra_off;    /* first unread extra byte */
    size_t extra_end;    /* end of the extra bytes */
    wtls_http_head head; /* points into buf */
    wtls_http_framing fr;
    uint64_t remaining;  /* WTLS_BODY_LENGTH */
    wtls_chunked chunked;
    int err;             /* sticky read error */
    bool eof;            /* body read to its end */
    bool head_request;
    bool is_http2;
    char *url;
    uint8_t redirects;
    bool reused;
    bool secure;
    bool insecure;       /* insecure_skip_verify: part of the pool key */
    bool has_cred;       /* client certificate identity: part of the pool key */
    uint8_t cred_fp[WTLS_HTTP_POOL_FP_LEN];
    uint16_t port;
    char host[256];
    wtls_conn_info info;
    wtls_abort_cb abort_cb;
    void *abort_ctx;
    wtls_http_progress_cb progress_cb;
    void *progress_ctx;
    size_t progress_done;
    int64_t progress_total;
#if WTLS_GZIP
    wtls_gzip *gz;       /* decoding Content-Encoding: gzip or deflate */
    bool is_deflate;
    uint8_t *gz_in;      /* raw body bytes waiting for the decoder */
    size_t gz_off, gz_end;
    bool gz_raw_end;     /* the raw body has ended */
#endif
};

/* ---- transport ------------------------------------------------------------ */

#define DEF_CONNECT_MS 5000u
#define DEF_IO_MS 15000u

static int plain_poll_wait(http_tr *t, bool rd, bool wr, uint64_t deadline) {
    for (;;) {
        if (t->abort_cb && t->abort_cb(t->abort_ctx)) return WTLS_ERR_CANCELLED;
        uint64_t now = wtls_plat_now_ms();
        if (now >= deadline) return WTLS_ERR_TIMEOUT;
        uint64_t left = deadline - now;
        uint32_t slice = (t->abort_cb && left > WTLS_POLL_SLICE_MS) ? WTLS_POLL_SLICE_MS : (uint32_t)left;
        int p = wtls_plat_poll(t->fd, rd, wr, slice);
        if (p < 0) return WTLS_ERR_IO;
        if (p > 0) return WTLS_OK;
        if (!t->abort_cb) return WTLS_ERR_TIMEOUT;
    }
}

/* Plain TCP read: bytes, WTLS_ERR_CLOSED at the end of the stream (plain HTTP
 * has no close_notify: a TCP end is the end), WTLS_ERR_TIMEOUT, WTLS_ERR_IO,
 * or WTLS_ERR_CANCELLED. */
static int plain_read(http_tr *t, void *buf, size_t len) {
    if (len > WTLS_PLAT_MAX_CHUNK) len = WTLS_PLAT_MAX_CHUNK;
    uint64_t deadline = wtls_plat_now_ms() + t->io_timeout_ms;
    for (;;) {
        if (t->abort_cb && t->abort_cb(t->abort_ctx)) return WTLS_ERR_CANCELLED;
        int k = wtls_plat_recv(t->fd, buf, len);
        if (k > 0) return k;
        if (k == 0) return WTLS_ERR_CLOSED;
        if (k != WTLS_WANT_READ) return WTLS_ERR_IO;
        int st = plain_poll_wait(t, true, false, deadline);
        if (st != WTLS_OK) return st;
    }
}

static int plain_write(http_tr *t, const void *buf, size_t len) {
    if (len > WTLS_PLAT_MAX_CHUNK) len = WTLS_PLAT_MAX_CHUNK;
    uint64_t deadline = wtls_plat_now_ms() + t->io_timeout_ms;
    for (;;) {
        if (t->abort_cb && t->abort_cb(t->abort_ctx)) return WTLS_ERR_CANCELLED;
        int k = wtls_plat_send(t->fd, buf, len);
        if (k > 0) return k;
        if (k != WTLS_WANT_WRITE) return WTLS_ERR_IO;
        int st = plain_poll_wait(t, false, true, deadline);
        if (st != WTLS_OK) return st;
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

static void tr_set_abort(http_tr *t, wtls_abort_cb cb, void *ctx) {
    if (t->tls) {
        t->tls->abort_cb = cb;
        t->tls->abort_ctx = ctx;
    } else {
        t->abort_cb = cb;
        t->abort_ctx = ctx;
    }
}

static void tr_mark_aborted(http_tr *t) {
    if (t && t->tls) t->tls->aborted = true;
}

void wtls_http_tr_close(void *c) {
    http_tr *t = c;
    if (!t) return;
#if WTLS_HTTP2
    wtls_h2_free(t->h2);
#endif
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
#if WTLS_HTTP2
    if (t->h2) return wtls_h2_alive(t->h2);
#endif
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
        if (opt->abort_cb && opt->abort_cb(opt->abort_ctx)) {
            free(t);
            return WTLS_ERR_CANCELLED;
        }
        st = wtls_connect_tcp(
            host, port, opt->connect_timeout_ms ? opt->connect_timeout_ms : DEF_CONNECT_MS,
            opt->abort_cb, opt->abort_ctx, &t->fd, NULL, NULL);
        if (st != WTLS_OK) t->fd = -1;
        t->io_timeout_ms = opt->io_timeout_ms ? opt->io_timeout_ms : DEF_IO_MS;
        t->abort_cb = opt->abort_cb;
        t->abort_ctx = opt->abort_ctx;
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
    wtls_http_body_cb body_cb; /* NULL: no streaming body; mutual with body */
    void *body_ctx;
    bool body_chunked;
    bool cb_started;   /* body_cb produced at least one byte on this hop */
    const wtls_http_header *headers;
    size_t n_headers;
    bool want_gzip; /* offer gzip/deflate and decode it */
    bool allow_upgrade;
    bool expect_100;
    bool early_final; /* server answered >= 200 before body was sent (Expect: 100-continue) */
    bool has_proxy;
    bool want_h2;
    const wtls_url *url;
    char proxy_host[256];
    uint16_t proxy_port;
    const char *head;
    size_t head_len;
    wtls_options opt;
} request_ctx;

/* Streams the request body from the callback after the head: exactly
 * body_len bytes with Content-Length, or <hex>\r\n<data>\r\n chunks ended by
 * the callback returning 0 (which sends 0\r\n\r\n). A callback that stops
 * early, writes more than asked or returns <0 aborts the request with
 * WTLS_ERR_IO; the caller then closes the connection (never pools it). */
static int send_body_cb(http_tr *c, request_ctx *x) {
    if (!x->body_chunked && x->body_len == 0) return WTLS_OK;
    /* Heap-allocated to keep 16 KiB off Wii LWP network-thread stacks. */
    uint8_t *buf = malloc(UPLOAD_BUF_SIZE);
    if (!buf) return WTLS_ERR_NOMEM;
    size_t left = x->body_len;
    int ret = WTLS_OK;
    for (;;) {
        if (x->opt.abort_cb && x->opt.abort_cb(x->opt.abort_ctx)) {
            tr_mark_aborted(c);
            ret = WTLS_ERR_CANCELLED;
            break;
        }
        size_t want = UPLOAD_BUF_SIZE;
        if (!x->body_chunked && want > left) want = left;
        x->cb_started = true;
        int k = x->body_cb(x->body_ctx, buf, want);
        if (k < 0 || (size_t)k > want) {
            ret = WTLS_ERR_IO;
            break;
        }
        if (!x->body_chunked) {
            if (k == 0) {
                ret = WTLS_ERR_IO; /* short of body_len: no rewind */
                break;
            }
            int st = tr_write_all(c, buf, (size_t)k);
            if (st != WTLS_OK) {
                ret = st;
                break;
            }
            left -= (size_t)k;
            if (!left) break;
            continue;
        }
        if (k == 0) {
            ret = tr_write_all(c, "0\r\n\r\n", 5);
            break;
        }
        char hex[10];
        int n = snprintf(hex, sizeof hex, "%x\r\n", (unsigned)k);
        int st = tr_write_all(c, hex, (size_t)n);
        if (st != WTLS_OK) {
            ret = st;
            break;
        }
        st = tr_write_all(c, buf, (size_t)k);
        if (st != WTLS_OK) {
            ret = st;
            break;
        }
        st = tr_write_all(c, "\r\n", 2);
        if (st != WTLS_OK) {
            ret = st;
            break;
        }
    }
    free(buf);
    return ret;
}

static int send_request(http_tr *c, request_ctx *x) {
    if (x->opt.abort_cb && x->opt.abort_cb(x->opt.abort_ctx)) {
        tr_mark_aborted(c);
        return WTLS_ERR_CANCELLED;
    }
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
    if (st != WTLS_OK) return st;
    if (x->body_cb) return send_body_cb(c, x);
    if (body_len) st = tr_write_all(c, x->body, body_len);
    return st;
}

#define EXPECT_CONTINUE_TIMEOUT_MS 1000u

/* Reads a response head (reusing r->buf when *fill_io > 0).
 * When stop_on_100 is true: returns WTLS_OK with r->head.status == 100 if 100
 * Continue arrived, r->head.status == 0 if timeout_ms elapsed with 0 bytes, or
 * r->head.status >= 200 (or 101 if allow_upgrade) if an early final response arrived. */
static int read_head_ex(wtls_http_response *r, uint32_t timeout_ms, uint32_t header_timeout_ms,
                        bool allow_upgrade, bool stop_on_100, size_t *cap_io, size_t *fill_io,
                        bool *got) {
    size_t cap = *cap_io, fill = *fill_io;
    if (!r->buf || cap < HEAD_BUF_START) {
        free(r->buf);
        cap = HEAD_BUF_START;
        r->buf = malloc(cap);
        if (!r->buf) return WTLS_ERR_NOMEM;
        fill = 0;
    }
    char *buf = r->buf;
    uint64_t deadline = wtls_plat_now_ms() + timeout_ms;
    bool extended = false;
    int interim = 0;
    memset(&r->head, 0, sizeof r->head);
    for (;;) {
        if (r->abort_cb && r->abort_cb(r->abort_ctx)) {
            tr_mark_aborted(r->conn);
            return WTLS_ERR_CANCELLED;
        }
        size_t end = wtls_http_head_end(buf, fill);
        if (end) {
            int st = wtls_http_parse_head(buf, end, &r->head);
            if (st != WTLS_OK) return st;
            if (r->head.status >= 200 || (r->head.status == 101 && allow_upgrade)) {
                r->extra_off = end;
                r->extra_end = fill;
                *cap_io = cap;
                *fill_io = fill;
                return WTLS_OK;
            }
            if (r->head.status == 101) return WTLS_ERR_HTTP; /* no upgrade was asked for */
            if (stop_on_100 && r->head.status == 100) {
                memmove(buf, buf + end, fill - end);
                fill -= end;
                *cap_io = cap;
                *fill_io = fill;
                return WTLS_OK;
            }
            if (++interim > 16) return WTLS_ERR_HTTP_LIMIT;
            memmove(buf, buf + end, fill - end); /* interim 1xx: skip it */
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
        if (now >= deadline) {
            if (stop_on_100 && fill == 0) {
                *cap_io = cap;
                *fill_io = 0;
                return WTLS_OK;
            }
            if (stop_on_100 && !extended) {
                extended = true;
                deadline = now + header_timeout_ms;
            } else {
                return WTLS_ERR_TIMEOUT;
            }
        }
        tr_set_timeout(r->conn, (uint32_t)(deadline - now));
        int k = tr_read(r, buf + fill, cap - fill);
        if (k > 0) {
            fill += (size_t)k;
            *got = true;
        } else if (k == WTLS_ERR_TIMEOUT && stop_on_100) {
            if (fill == 0) {
                *cap_io = cap;
                *fill_io = 0;
                return WTLS_OK;
            }
            if (!extended) {
                extended = true;
                deadline = wtls_plat_now_ms() + header_timeout_ms;
                continue;
            }
            return WTLS_ERR_TIMEOUT;
        } else if (k == WTLS_ERR_CLOSED) {
            return WTLS_ERR_IO; /* closed before a complete head */
        } else {
            return k;
        }
    }
}

static int send_body_only(http_tr *c, request_ctx *x) {
    if (x->body_cb) return send_body_cb(c, x);
    if (x->body && x->body_len) return tr_write_all(c, x->body, x->body_len);
    return WTLS_OK;
}

static int connect_proxy_tunnel(wtls_http_response *r, request_ctx *x) {
    http_tr *t = calloc(1, sizeof *t);
    if (!t) return WTLS_ERR_NOMEM;
    t->fd = -1;
    t->io_timeout_ms = x->opt.io_timeout_ms ? x->opt.io_timeout_ms : DEF_IO_MS;
    t->abort_cb = x->opt.abort_cb;
    t->abort_ctx = x->opt.abort_ctx;
    int st = wtls_connect_tcp(
        x->proxy_host, x->proxy_port,
        x->opt.connect_timeout_ms ? x->opt.connect_timeout_ms : DEF_CONNECT_MS,
        x->opt.abort_cb, x->opt.abort_ctx, &t->fd, NULL, NULL);
    if (st != WTLS_OK) {
        free(t);
        return st;
    }
    if (!r->secure) {
        r->conn = t;
        return WTLS_OK;
    }
    char creq[340];
    int n = snprintf(creq, sizeof creq, "CONNECT %s:%u HTTP/1.1\r\nHost: %s:%u\r\n\r\n",
                     r->host, (unsigned)r->port, r->host, (unsigned)r->port);
    if (n <= 0 || (size_t)n >= sizeof creq) {
        wtls_http_tr_close(t);
        return WTLS_ERR_ARG;
    }
    r->conn = t;
    st = tr_write_all(t, creq, (size_t)n);
    if (st == WTLS_OK) {
        bool got = false;
        size_t cap = 0, fill = 0;
        uint32_t hdr_t = x->req->header_timeout_ms ? x->req->header_timeout_ms
                                                   : WTLS_HTTP_DEFAULT_HEADER_TIMEOUT_MS;
        st = read_head_ex(r, hdr_t, hdr_t, false, false, &cap, &fill, &got);
        if (st == WTLS_OK && (r->head.status < 200 || r->head.status >= 300 || r->extra_end > r->extra_off))
            st = WTLS_ERR_CONNECT;
        free(r->buf);
        r->buf = NULL;
        r->extra_off = r->extra_end = 0;
    }
    if (st != WTLS_OK) {
        wtls_http_tr_close(t);
        r->conn = NULL;
        return st;
    }
    int fd = t->fd;
    t->fd = -1;
    st = wtls_connect_on_fd(&t->tls, fd, r->host, r->port, &x->opt);
    if (st != WTLS_OK) {
        free(t);
        r->conn = NULL;
        return st;
    }
    return WTLS_OK;
}

static int connect_fresh(wtls_http_response *r, request_ctx *x) {
    r->reused = false;
    int st = x->has_proxy ? connect_proxy_tunnel(r, x)
                          : tr_connect(&r->conn, r->secure, r->host, r->port, &x->opt);
    if (st != WTLS_OK) {
        r->conn = NULL;
        return st;
    }
#if WTLS_HTTP2
    if (r->conn->tls) {
        wtls_conn_info ci;
        if (wtls_get_info(r->conn->tls, &ci) == WTLS_OK && ci.alpn && strcmp(ci.alpn, "h2") == 0) {
            st = wtls_h2_create(r->conn->tls, &r->conn->h2);
            if (st != WTLS_OK) {
                wtls_http_tr_close(r->conn);
                r->conn = NULL;
                return st;
            }
        }
    }
#endif
    return WTLS_OK;
}

static bool take_live(wtls_http_response *r, request_ctx *x, bool h2) {
    void *c;
    while ((c = wtls_http_pool_take(r->secure, r->host, r->port, r->insecure, r->has_cred,
                                    r->has_cred ? r->cred_fp : NULL, h2, wtls_plat_now_ms()))) {
        if (tr_alive(c)) {
            r->conn = c;
            r->reused = true;
            tr_set_timeout(c, x->opt.io_timeout_ms ? x->opt.io_timeout_ms : DEF_IO_MS);
            tr_set_abort(c, x->opt.abort_cb, x->opt.abort_ctx);
            return true;
        }
        wtls_http_tr_close(c);
    }
    return false;
}

/* A live pooled connection for the key, else a new one. */
static int acquire(wtls_http_response *r, request_ctx *x) {
    if (x->has_proxy) return connect_fresh(r, x);
#if WTLS_HTTP2
    if (x->want_h2 && r->secure && take_live(r, x, true)) return WTLS_OK;
#endif
    if (take_live(r, x, false)) return WTLS_OK;
    return connect_fresh(r, x);
}

#if WTLS_HTTP2
static int send_body_cb_h2(wtls_http_response *r, request_ctx *x, uint32_t hdr_to, bool *got) {
    wtls_h2_conn *h2 = r->conn->h2;
    if (!x->body_chunked && x->body_len == 0) return WTLS_OK;
    uint8_t *buf = malloc(UPLOAD_BUF_SIZE);
    if (!buf) return WTLS_ERR_NOMEM;
    size_t left = x->body_len;
    int ret = WTLS_OK;
    (void)hdr_to;
    for (;;) {
        if (x->opt.abort_cb && x->opt.abort_cb(x->opt.abort_ctx)) {
            tr_mark_aborted(r->conn);
            ret = WTLS_ERR_CANCELLED;
            break;
        }
        size_t want = UPLOAD_BUF_SIZE;
        if (!x->body_chunked && want > left) want = left;
        x->cb_started = true;
        int k = x->body_cb(x->body_ctx, buf, want);
        if (k < 0 || (size_t)k > want) {
            ret = WTLS_ERR_IO;
            break;
        }
        if (!x->body_chunked && k == 0) {
            ret = WTLS_ERR_IO;
            break;
        }
        bool end_stream = x->body_chunked ? (k == 0) : (left == (size_t)k);
        int st = wtls_h2_send_data(h2, buf, (size_t)k, end_stream, &x->early_final,
                                   &r->head, &r->buf, got);
        if (st != WTLS_OK || x->early_final || end_stream) {
            ret = st;
            break;
        }
        if (!x->body_chunked) left -= (size_t)k;
    }
    free(buf);
    return ret;
}

static int exchange_h2(wtls_http_response *r, request_ctx *x, bool *got) {
    wtls_h2_conn *h2 = r->conn->h2;
    /* An earlier hop (redirect) leaves its head in r->buf; the HPACK decoder
     * hands out a new arena and would overwrite the pointer. */
    free(r->buf);
    r->buf = NULL;
    memset(&r->head, 0, sizeof r->head); /* its fields pointed into r->buf */
    uint32_t io_timeout = tr_timeout(r->conn);
    uint32_t t = x->req->header_timeout_ms ? x->req->header_timeout_ms : WTLS_HTTP_DEFAULT_HEADER_TIMEOUT_MS;
    int st = wtls_h2_begin_stream(h2);
    if (st != WTLS_OK) return st;
    if (x->opt.abort_cb && x->opt.abort_cb(x->opt.abort_ctx)) {
        tr_mark_aborted(r->conn);
        return WTLS_ERR_CANCELLED;
    }
    bool has_body = (x->body != NULL || x->body_cb != NULL);
    bool has_cl = !x->body_chunked && (has_body || strcmp(x->method, "POST") == 0 ||
                                       strcmp(x->method, "PUT") == 0 || strcmp(x->method, "PATCH") == 0);
    size_t cl_len = has_body ? x->body_len : 0;
    st = wtls_h2_send_headers(h2, x->method, x->url, x->headers, x->n_headers, has_cl, cl_len,
                              has_body, x->want_gzip, x->expect_100);
    if (st == WTLS_OK && x->expect_100) {
        st = wtls_h2_wait_continue(h2, EXPECT_CONTINUE_TIMEOUT_MS, t, &x->early_final,
                                   &r->head, &r->buf, got);
    }
    if (st == WTLS_OK && !x->early_final) {
        if (x->body_cb) {
            st = send_body_cb_h2(r, x, t, got);
        } else if (x->body && x->body_len) {
            st = wtls_h2_send_data(h2, x->body, x->body_len, true, &x->early_final,
                                   &r->head, &r->buf, got);
        }
    }
    if (st == WTLS_OK && !x->early_final) {
        st = wtls_h2_read_head(h2, t, &r->head, &r->buf, got);
    }
    tr_set_timeout(r->conn, io_timeout);
    return st;
}
#endif

/* Request out, final response head in. The connection's own I/O timeout is
 * restored afterwards (the head phase runs on the header deadline, which
 * starts only after the whole request body has been streamed out). */
static int exchange(wtls_http_response *r, request_ctx *x, bool *got) {
    *got = false;
    x->cb_started = false;
    x->early_final = false;
    if (r->conn->tls) wtls_get_info(r->conn->tls, &r->info);
    else memset(&r->info, 0, sizeof r->info);
#if WTLS_HTTP2
    if (r->conn->h2) return exchange_h2(r, x, got);
#endif
    uint32_t io_timeout = tr_timeout(r->conn);
    uint32_t t = x->req->header_timeout_ms ? x->req->header_timeout_ms : WTLS_HTTP_DEFAULT_HEADER_TIMEOUT_MS;
    size_t cap = 0, fill = 0;
    int st;
    if (x->expect_100) {
        if (x->opt.abort_cb && x->opt.abort_cb(x->opt.abort_ctx)) {
            tr_mark_aborted(r->conn);
            return WTLS_ERR_CANCELLED;
        }
        st = tr_write_all(r->conn, x->head, x->head_len);
        if (st == WTLS_OK) {
            st = read_head_ex(r, EXPECT_CONTINUE_TIMEOUT_MS, t, x->allow_upgrade, true, &cap, &fill, got);
            if (st == WTLS_OK && r->head.status >= 200) {
                x->early_final = true;
                tr_set_timeout(r->conn, io_timeout);
                return WTLS_OK;
            }
            if (st == WTLS_OK && r->head.status == 101 && x->allow_upgrade) {
                x->early_final = true;
                tr_set_timeout(r->conn, io_timeout);
                return WTLS_OK;
            }
        }
        if (st == WTLS_OK) {
            tr_set_timeout(r->conn, io_timeout);
            st = send_body_only(r->conn, x);
        }
    } else {
        st = send_request(r->conn, x);
    }
    if (st == WTLS_OK)
        st = read_head_ex(r, t, t, x->allow_upgrade, false, &cap, &fill, got);
    tr_set_timeout(r->conn, io_timeout);
    return st;
}

/* Pools the connection if its response was read to its end, may carry another
 * request and holds no unread bytes; closes it otherwise. */
static void release_conn(wtls_http_response *r) {
    if (!r->conn) return;
    tr_set_abort(r->conn, NULL, NULL);
    r->abort_cb = NULL;
    r->abort_ctx = NULL;
    bool h2 = false;
    bool h2_ok = true;
#if WTLS_HTTP2
    if (r->conn->h2) {
        h2 = true;
        h2_ok = wtls_h2_reusable(r->conn->h2) && wtls_h2_stream_eof(r->conn->h2);
    }
#endif
    if (r->eof && !r->err && r->fr.reusable && r->extra_off == r->extra_end && h2_ok) {
        wtls_shutdown_hook = wtls_http_pool_flush;
        wtls_pool_flush_hook = wtls_http_pool_flush;
        wtls_http_pool_put(r->conn, r->secure, r->host, r->port, r->insecure, r->has_cred,
                           r->has_cred ? r->cred_fp : NULL, h2, wtls_plat_now_ms());
    } else {
        wtls_http_tr_close(r->conn);
    }
    r->conn = NULL;
}

/* Reads a redirect body away so the connection can be reused. A body of
 * unknown length or over WTLS_HTTP_REDIRECT_DRAIN_MAX stays unread, and
 * release_conn() then closes the connection. */
static int drain_body(wtls_http_response *r) {
    if (r->fr.kind == WTLS_BODY_CLOSE) return WTLS_OK;
    if (r->fr.kind == WTLS_BODY_LENGTH && r->fr.length > WTLS_HTTP_REDIRECT_DRAIN_MAX) return WTLS_OK;
    char tmp[1024];
    size_t total = 0;
    while (!r->eof && total <= WTLS_HTTP_REDIRECT_DRAIN_MAX) {
        int k = wtls_http_read(r, tmp, sizeof tmp);
        if (k == WTLS_ERR_CANCELLED) return WTLS_ERR_CANCELLED;
        if (k <= 0) return WTLS_OK;
        total += (size_t)k;
    }
    return WTLS_OK;
}

#if WTLS_HTTP2
static int frame_h2(wtls_http_response *r, bool want_keepalive, bool early_final, bool has_proxy) {
    memset(&r->fr, 0, sizeof r->fr);
    r->fr.reusable = want_keepalive && !early_final && !has_proxy && wtls_h2_reusable(r->conn->h2);
    if (r->head_request || r->head.status == 204 || r->head.status == 304) {
        r->fr.kind = WTLS_BODY_NONE;
        return WTLS_OK;
    }
    const char *cl = NULL;
    for (size_t i = 0; i < r->head.n_fields; i++) {
        if (strcmp(r->head.fields[i].name, "content-length") == 0) {
            if (cl && strcmp(cl, r->head.fields[i].value) != 0) return WTLS_ERR_HTTP;
            cl = r->head.fields[i].value;
        }
    }
    if (!cl) {
        r->fr.kind = WTLS_BODY_CLOSE;
        return WTLS_OK;
    }
    if (!*cl) return WTLS_ERR_HTTP;
    uint64_t val = 0;
    for (const char *p = cl; *p; p++) {
        if (*p < '0' || *p > '9') return WTLS_ERR_HTTP;
        uint64_t d = (uint64_t)(*p - '0');
        if (val > (UINT64_MAX - d) / 10u) return WTLS_ERR_HTTP;
        val = val * 10u + d;
    }
    r->fr.kind = WTLS_BODY_LENGTH;
    r->fr.length = val;
    return WTLS_OK;
}
#endif

/* One request/response exchange against *u: head built, connection acquired
 * (pooled or new), final head read and framed. */
static int hop(wtls_http_response *r, request_ctx *x, const wtls_url *u) {
    char *head = NULL;
    unsigned req_flags = (x->body_chunked ? WTLS_HTTP_REQ_CHUNKED : 0u) |
                         (x->req->no_keepalive ? WTLS_HTTP_REQ_NO_KEEPALIVE : 0u) |
                         (x->want_gzip ? WTLS_HTTP_REQ_GZIP : 0u) |
                         (x->allow_upgrade ? WTLS_HTTP_REQ_UPGRADE : 0u) |
                         (x->expect_100 ? WTLS_HTTP_REQ_EXPECT_100 : 0u) |
                         (x->has_proxy ? WTLS_HTTP_REQ_PROXY : 0u);
    int st = wtls_http_build_head(x->method, u, x->headers, x->n_headers, x->body != NULL || x->body_cb != NULL,
                                  x->body_len, req_flags, &head, &x->head_len);
    if (st != WTLS_OK) return st;
    x->head = head;
    x->url = u;
    r->secure = u->secure;
    r->port = u->port;
    memcpy(r->host, u->host, sizeof r->host);
    /* this hop's client + pin identity: part of the pool key for HTTPS hops */
    if (u->secure) {
        st = wtls_conn_identity_fp(&x->opt, &r->has_cred, r->cred_fp);
        if (st != WTLS_OK) {
            free(head);
            x->head = NULL;
            return st;
        }
    } else {
        r->has_cred = false;
    }
    r->head_request = strcmp(x->method, "HEAD") == 0;
    r->is_http2 = false;
    r->err = 0;
    r->eof = false;
    r->extra_off = r->extra_end = 0;

    bool got = false;
    st = acquire(r, x);
    if (st == WTLS_OK) st = exchange(r, x, &got);
    /* The server closed a reused connection as the request went out: one
     * second attempt on a new connection, for idempotent methods only, and
     * with a streaming body only if the callback was never called (the
     * abort happened while the head was still being written - a callback
     * that has produced bytes cannot be rewound). */
    if (st == WTLS_ERR_IO && r->reused && !got && !x->cb_started &&
        wtls_http_method_idempotent(x->method)) {
        wtls_http_tr_close(r->conn);
        r->conn = NULL;
        st = connect_fresh(r, x);
        if (st == WTLS_OK) st = exchange(r, x, &got);
    }
    free(head);
    x->head = NULL;
    if (st != WTLS_OK) return st;
#if WTLS_HTTP2
    if (r->conn->h2) {
        r->is_http2 = true;
        st = frame_h2(r, !x->req->no_keepalive, x->early_final, x->has_proxy);
        if (st != WTLS_OK) return st;
        r->remaining = r->fr.length;
        r->eof = r->fr.kind == WTLS_BODY_NONE || (r->fr.kind == WTLS_BODY_LENGTH && r->fr.length == 0);
        return WTLS_OK;
    }
#endif
    st = wtls_http_frame(&r->head, r->head_request, !x->req->no_keepalive, &r->fr);
    if (st != WTLS_OK) return st;
    if (r->head.status == 101 || x->early_final || x->has_proxy) r->fr.reusable = false;
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

    if (!*filtered && !wtls_url_same_origin(u, &nu)) {
        /* The next hop leaves the origin: strip credentials, both the
         * header credentials and the client certificate, which must never
         * be presented to a different origin. */
        x->opt.client_cred = NULL;
        if (x->n_headers) {
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
    }

    int s = r->head.status;
    bool dropped = false;
    if ((s == 303 && !r->head_request) || ((s == 301 || s == 302) && strcmp(x->method, "POST") == 0)) {
        x->method = "GET";
        x->body = NULL;
        x->body_len = 0;
        dropped = true;
    }
    /* A streaming body cannot be rewound: a redirect that keeps the request
     * body (307/308 always, 301/302 with methods other than POST) aborts
     * with WTLS_ERR_HTTP_REDIRECT; applications with their own rewind set
     * max_redirects = -1 and follow Location themselves. When the body is
     * dropped, the callback is not called for the follow-up request. */
    if (x->body_cb) {
        if (!dropped) {
            free(next);
            return WTLS_ERR_HTTP_REDIRECT;
        }
        x->body_cb = NULL;
        x->body_ctx = NULL;
        x->body_chunked = false;
        x->body_len = 0;
        x->expect_100 = false;
    }
    if (dropped) x->expect_100 = false;

    /* location points into r->buf, which the next hop replaces: use it first. */
    if (drain_body(r) == WTLS_ERR_CANCELLED) {
        free(next);
        return WTLS_ERR_CANCELLED;
    }
    release_conn(r);
    free(r->url);
    r->url = next;
    *u = nu; /* nu.target points into next, now r->url */
    r->redirects++;
    return WTLS_OK;
}

#define GZ_IN_BUF 16384u

/* Content-Encoding of the final response. Only when gzip/deflate was offered:
 * gzip, x-gzip or deflate is decoded, identity or no field is raw, anything
 * else is an error. Redirect bodies are drained raw, so only the final
 * response gets here. */
static int setup_encoding(wtls_http_response *r, bool want_gzip) {
#if WTLS_GZIP
    if (!want_gzip || r->fr.kind == WTLS_BODY_NONE) return WTLS_OK;
    const char *ce = wtls_http_head_get(&r->head, "content-encoding");
    if (!ce || !*ce || wtls_http_ieq(ce, "identity")) return WTLS_OK;
    if (wtls_http_ieq(ce, "gzip") || wtls_http_ieq(ce, "x-gzip")) {
        r->gz = wtls_gzip_new();
        r->is_deflate = false;
    } else if (wtls_http_ieq(ce, "deflate")) {
        r->gz = wtls_deflate_new();
        r->is_deflate = true;
    } else {
        return WTLS_ERR_HTTP;
    }
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
    if (!wtls_core_initialized()) return WTLS_ERR_STATE;
    if (!WTLS_THREAD_GUARD("wtls_http_open")) return WTLS_ERR_STATE;
    if (wtls_plat_net_changed()) wtls_net_reset();
    if (req->n_headers && !req->headers) return WTLS_ERR_ARG;
    if (req->body_len && !req->body && !req->body_cb) return WTLS_ERR_ARG;
    if (req->body && req->body_cb) return WTLS_ERR_ARG;
    if (req->body_chunked && !req->body_cb) return WTLS_ERR_ARG;

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
                     .body_cb = req->body_cb,
                     .body_ctx = req->body_ctx,
                     .body_chunked = req->body_chunked,
                     .headers = req->headers,
                     .n_headers = req->n_headers,
                     .allow_upgrade = req->allow_upgrade,
                     .expect_100 = req->expect_100_continue &&
                                   (req->body_cb != NULL || (req->body != NULL && req->body_len > 0))};
#if WTLS_GZIP
    /* An application that sets Accept-Encoding itself gets the body as sent. */
    x.want_gzip = !req->allow_upgrade && !req->no_gzip &&
                  !wtls_http_headers_have(req->headers, req->n_headers, "Accept-Encoding");
#endif
    if (req->proxy_url) {
        size_t plen = strnlen(req->proxy_url, WTLS_HTTP_MAX_URL + 1);
        if (plen > WTLS_HTTP_MAX_URL) {
            response_free(r);
            return WTLS_ERR_ARG;
        }
        char *pcopy = malloc(plen + 1);
        if (!pcopy) {
            response_free(r);
            return WTLS_ERR_NOMEM;
        }
        memcpy(pcopy, req->proxy_url, plen + 1);
        wtls_url pu;
        int pst = wtls_url_parse(pcopy, &pu);
        bool bad_proxy = pst != WTLS_OK || pu.secure || (!pu.target_needs_slash && pu.target_len > 1) ||
                         (pu.target_len == 1 && pu.target[0] != '/');
        if (!bad_proxy) {
            x.has_proxy = true;
            memcpy(x.proxy_host, pu.host, sizeof x.proxy_host);
            x.proxy_port = pu.port;
        }
        free(pcopy);
        if (bad_proxy) {
            response_free(r);
            return WTLS_ERR_ARG;
        }
    }
    if (req->tls) x.opt = *req->tls;
    if (u.secure && x.opt.n_spki_pins > WTLS_SPKI_MAX_PINS) {
        response_free(r);
        return WTLS_ERR_ARG;
    }
#if WTLS_HTTP2
    x.want_h2 = !req->no_h2 && !req->allow_upgrade;
    x.opt.alpn = x.want_h2 ? k_alpn_h2 : k_alpn_h1;
#else
    x.opt.alpn = k_alpn_h1;
#endif
    r->insecure = x.opt.insecure_skip_verify;
    r->abort_cb = x.opt.abort_cb;
    r->abort_ctx = x.opt.abort_ctx;
    if (r->abort_cb && r->abort_cb(r->abort_ctx)) {
        response_free(r);
        return WTLS_ERR_CANCELLED;
    }

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
    r->progress_cb = req->progress_cb;
    r->progress_ctx = req->progress_ctx;
    r->progress_done = 0;
    r->progress_total = wtls_http_content_length(r);
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
#if WTLS_GZIP
    out->gzip = r->gz != NULL && !r->is_deflate;
    out->deflate = r->gz != NULL && r->is_deflate;
#endif
    out->http2 = r->is_http2;
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
#if WTLS_HTTP2
    if (r->conn && r->conn->h2) {
        bool stream_eof = false;
        int k = wtls_h2_read_data(r->conn->h2, buf, len, &stream_eof);
        if (k < 0) return fail(r, k);
        if (r->fr.kind == WTLS_BODY_LENGTH) {
            if ((uint64_t)k > r->remaining) return fail(r, WTLS_ERR_IO);
            r->remaining -= (uint64_t)k;
            if (stream_eof && r->remaining > 0) return fail(r, WTLS_ERR_IO);
            if (!r->remaining) r->eof = true;
        } else if (stream_eof) {
            r->eof = true;
        }
        return k;
    }
#endif
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
    if (!WTLS_THREAD_GUARD("wtls_http_read")) return WTLS_ERR_STATE;
    if (r->err) return r->err;
    if (r->abort_cb && r->abort_cb(r->abort_ctx)) {
        tr_mark_aborted(r->conn);
        return fail(r, WTLS_ERR_CANCELLED);
    }
    if (!len) return 0;
    if (len > INT_MAX) len = INT_MAX;
    int k;
#if WTLS_GZIP
    k = r->gz ? gz_read(r, buf, len) : raw_read(r, buf, len);
#else
    k = raw_read(r, buf, len);
#endif
    if (k > 0) {
        r->progress_done += (size_t)k;
        if (r->progress_cb) r->progress_cb(r->progress_ctx, r->progress_done, r->progress_total);
    }
    return k;
}

/* ---- close ------------------------------------------------------------------------- */

void wtls_http_close(wtls_http_response *r) {
    if (!r) return;
    if (!WTLS_THREAD_GUARD("wtls_http_close")) return;
    release_conn(r);
    response_free(r);
}

wtls_status wtls_http_detach(wtls_http_response *r, wtls_conn **tls_out, int *fd_out,
                             void *extra_buf, size_t extra_cap, size_t *extra_len) {
    if (!r || (!tls_out && !fd_out) || (extra_cap && !extra_buf)) return WTLS_ERR_ARG;
    if (!WTLS_THREAD_GUARD("wtls_http_detach")) return WTLS_ERR_STATE;
    if (!r->conn || r->head.status != 101 || r->is_http2) return WTLS_ERR_STATE;

    size_t avail = r->extra_end - r->extra_off;
    if (!extra_buf && extra_len) {
        *extra_len = avail;
        return WTLS_OK;
    }
    if (extra_buf) {
        if (extra_cap < avail) {
            if (extra_len) *extra_len = avail;
            return WTLS_ERR_HTTP_LIMIT;
        }
        if (avail) memcpy(extra_buf, r->buf + r->extra_off, avail);
        if (extra_len) *extra_len = avail;
    } else if (avail > 0) {
        if (!r->conn->tls) return WTLS_ERR_HTTP_LIMIT;
        uint8_t *p = mbedtls_calloc(1, avail);
        if (!p) return WTLS_ERR_NOMEM;
        memcpy(p, r->buf + r->extra_off, avail);
        r->conn->tls->preread = p;
        r->conn->tls->preread_off = 0;
        r->conn->tls->preread_len = avail;
    }

    if (tls_out) *tls_out = r->conn->tls;
    if (fd_out) *fd_out = r->conn->tls ? r->conn->tls->fd : r->conn->fd;
    free(r->conn);
    r->conn = NULL;
    r->extra_off = r->extra_end;
    return WTLS_OK;
}

/* ---- fetch --------------------------------------------------------------------------- */

#define FETCH_BUF_START 16384u

wtls_status wtls_http_fetch(const wtls_http_request *req, size_t max_body, wtls_http_result *out) {
    if (!out) return WTLS_ERR_ARG;
    memset(out, 0, sizeof *out);
    if (!WTLS_THREAD_GUARD("wtls_http_fetch")) return WTLS_ERR_STATE;
    if (req && req->tls && req->tls->abort_cb && req->tls->abort_cb(req->tls->abort_ctx))
        return WTLS_ERR_CANCELLED;
    if (!max_body) max_body = WTLS_HTTP_FETCH_DEFAULT_MAX;
    if (max_body > (size_t)INT_MAX - 1) max_body = (size_t)INT_MAX - 1;
    wtls_http_response *r;
    int st = wtls_http_open(&r, req);
    if (st != WTLS_OK) return st;
    out->status = r->head.status;
    int64_t total = wtls_http_content_length(r);

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
    if (st == WTLS_OK && fill == 0 && req->progress_cb) req->progress_cb(req->progress_ctx, 0, total);
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
