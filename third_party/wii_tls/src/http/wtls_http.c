/* HTTP/1.1 client: request, response head, body streaming (RFC 9112),
 * keep-alive pool reuse with a liveness check and one idempotent retry,
 * redirects, gzip/deflate (WTLS_GZIP), br (WTLS_BROTLI) and zstd (WTLS_ZSTD) Content-Encoding, over TLS (https) or plain
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

#include "wtls_mem.h"
#include "http/wtls_cookie.h"
#include "http/wtls_http_chunked.h"
#if WTLS_CODECS
#include "http/wtls_http_gzip.h"
#endif
#if WTLS_HTTP2
#include "http/wtls_h2s.h"
#include "http/wtls_http2.h"
#endif
#include "http/wtls_http_parse.h"
#include "http/wtls_http_pool.h"
#include "http/wtls_http_req.h"
#include "http/wtls_url.h"
#include "plat/plat.h"
#include "wtls_cred.h"
#include "wtls_dns.h"
#include "wtls_log.h"
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
    bool nonblock;          /* reads and writes return WTLS_WANT_* instead of waiting (wtls_http_start) */
#if WTLS_HTTP2
    wtls_h2_conn *h2;
#endif
} http_tr;

#if WTLS_HTTP2
typedef struct h2_sess h2_sess; /* a multiplexed HTTP/2 session of the asynchronous API (see below) */
#endif

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
    uint64_t deadline_ms; /* total_timeout_ms: absolute, 0 = none */
#if WTLS_HTTP2
    h2_sess *h2sess;     /* wtls_http_start over HTTP/2: the shared session and this response's stream; conn is NULL */
    h2s_stream *h2st;
#endif
    struct async_state *as; /* wtls_http_start: the request in flight until the head is complete */
    uint32_t io_timeout_ms; /* non-blocking body reads: longest wait without any byte */
    uint64_t last_io_ms;
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
#if WTLS_CODECS
    wtls_gzip *gz;       /* decoding Content-Encoding: gzip or deflate */
    bool is_deflate;
    int enc_kind; /* 0 none, 1 gzip, 2 deflate, 3 br, 4 zstd */
    uint8_t *gz_in;      /* raw body bytes waiting for the decoder */
    size_t gz_off, gz_end;
    bool gz_raw_end;     /* the raw body has ended */
#endif
};

/* ---- transport ------------------------------------------------------------ */

#define DEF_CONNECT_MS 5000u
#define DEF_IO_MS 15000u
#define DEF_HANDSHAKE_MS 10000u

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
    if (t->nonblock) {
        if (t->abort_cb && t->abort_cb(t->abort_ctx)) return WTLS_ERR_CANCELLED;
        int k = wtls_plat_recv(t->fd, buf, len);
        if (k > 0) return k;
        if (k == 0) return WTLS_ERR_CLOSED;
        return k == WTLS_WANT_READ ? WTLS_WANT_READ : WTLS_ERR_IO;
    }
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
    if (t->nonblock) {
        if (t->abort_cb && t->abort_cb(t->abort_ctx)) return WTLS_ERR_CANCELLED;
        int k = wtls_plat_send(t->fd, buf, len);
        if (k > 0) return k;
        return k == WTLS_WANT_WRITE ? WTLS_WANT_WRITE : WTLS_ERR_IO;
    }
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
    wtls_free(t);
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
    http_tr *t = wtls_calloc(1, sizeof *t);
    if (!t) return WTLS_ERR_NOMEM;
    t->fd = -1;
    int st;
    if (secure) {
        st = wtls_connect(&t->tls, host, port, opt);
    } else {
        if (opt->abort_cb && opt->abort_cb(opt->abort_ctx)) {
            wtls_free(t);
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
        wtls_free(t);
        return st;
    }
    *out = t;
    return WTLS_OK;
}

/* ---- open ------------------------------------------------------------------- */

static void async_free(struct async_state *as); /* wtls_http_async.inc */

static void response_free(wtls_http_response *r) {
    if (!r) return;
    async_free(r->as);
    r->as = NULL;
    if (r->conn) wtls_http_tr_close(r->conn);
#if WTLS_CODECS
    wtls_gzip_free(r->gz);
    wtls_free(r->gz_in);
#endif
    wtls_free(r->buf);
    wtls_free(r->url);
    wtls_free(r);
}

static bool decoding(const wtls_http_response *r) {
#if WTLS_CODECS
    return r->gz != NULL;
#else
    (void)r;
    return false;
#endif
}

/* One request as it goes out on the current hop: a redirect may change the
 * method, drop the body and strip credential headers. */
enum { PROXY_HTTP = 1, PROXY_SOCKS5 = 2, PROXY_SOCKS5H = 3 };

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
    bool has_proxy;   /* any proxy: no pooling */
    int proxy_kind;   /* PROXY_HTTP / PROXY_SOCKS5 / PROXY_SOCKS5H */
    bool proxy_auth;
    char proxy_user[256], proxy_pass[256];
    char proxy_basic[700]; /* "Basic ..." for an HTTP proxy */
    bool want_h2;
    const wtls_url *url;
    char proxy_host[256];
    uint16_t proxy_port;
    const char *head;
    size_t head_len;
    wtls_options opt;
    uint32_t header_timeout; /* request sent -> head complete, after the total budget */
} request_ctx;

/* Streams the request body from the callback after the head: exactly
 * body_len bytes with Content-Length, or <hex>\r\n<data>\r\n chunks ended by
 * the callback returning 0 (which sends 0\r\n\r\n). A callback that stops
 * early, writes more than asked or returns <0 aborts the request with
 * WTLS_ERR_IO; the caller then closes the connection (never pools it). */
static int send_body_cb(http_tr *c, request_ctx *x) {
    if (!x->body_chunked && x->body_len == 0) return WTLS_OK;
    /* Heap-allocated to keep 16 KiB off Wii LWP network-thread stacks. */
    uint8_t *buf = wtls_malloc(UPLOAD_BUF_SIZE);
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
    wtls_free(buf);
    return ret;
}

static int send_request(http_tr *c, request_ctx *x) {
    if (x->opt.abort_cb && x->opt.abort_cb(x->opt.abort_ctx)) {
        tr_mark_aborted(c);
        return WTLS_ERR_CANCELLED;
    }
    size_t body_len = x->body ? x->body_len : 0;
    if (body_len && x->head_len + body_len <= SINGLE_WRITE_MAX) {
        char *all = wtls_malloc(x->head_len + body_len);
        if (!all) return WTLS_ERR_NOMEM;
        memcpy(all, x->head, x->head_len);
        memcpy(all + x->head_len, x->body, body_len);
        int st = tr_write_all(c, all, x->head_len + body_len);
        wtls_free(all);
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
        wtls_free(r->buf);
        cap = HEAD_BUF_START;
        r->buf = wtls_malloc(cap);
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
            char *nb = wtls_realloc(buf, ncap);
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

static int pct_decode(const char *in, size_t n, char *out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        char c = in[i];
        if (c == '%') {
            if (i + 2 >= n) return -1;
            int hi = in[i + 1], lo = in[i + 2];
            hi = hi >= '0' && hi <= '9' ? hi - '0' : (hi | 0x20) >= 'a' && (hi | 0x20) <= 'f' ? (hi | 0x20) - 'a' + 10 : -1;
            lo = lo >= '0' && lo <= '9' ? lo - '0' : (lo | 0x20) >= 'a' && (lo | 0x20) <= 'f' ? (lo | 0x20) - 'a' + 10 : -1;
            if (hi < 0 || lo < 0) return -1;
            c = (char)(hi * 16 + lo);
            i += 2;
        }
        if (o + 1 >= cap) return -1;
        out[o++] = c;
    }
    out[o] = '\0';
    return (int)o;
}

/* "http://[user:pass@]host[:port]", "socks5://..." or "socks5h://...": fills the proxy fields of x. */
static int parse_proxy(const char *url, request_ctx *x) {
    int kind;
    const char *rest;
    uint16_t default_port;
    if (strncmp(url, "http://", 7) == 0) { kind = PROXY_HTTP; rest = url + 7; default_port = 80; }
    else if (strncmp(url, "socks5://", 9) == 0) { kind = PROXY_SOCKS5; rest = url + 9; default_port = 1080; }
    else if (strncmp(url, "socks5h://", 10) == 0) { kind = PROXY_SOCKS5H; rest = url + 10; default_port = 1080; }
    else return WTLS_ERR_ARG;
    size_t alen = strcspn(rest, "/");
    if (rest[alen] == '/' && rest[alen + 1] != '\0') return WTLS_ERR_ARG; /* no path on a proxy */
    const char *auth_end = rest + alen;
    const char *at = NULL;
    for (const char *q = rest; q < auth_end; q++)
        if (*q == '@') at = q;
    const char *hostport = at ? at + 1 : rest;
    if (at) {
        const char *colon = memchr(rest, ':', (size_t)(at - rest));
        size_t ul = colon ? (size_t)(colon - rest) : (size_t)(at - rest);
        if (ul == 0) return WTLS_ERR_ARG;
        if (pct_decode(rest, ul, x->proxy_user, sizeof x->proxy_user) < 0) return WTLS_ERR_ARG;
        if (colon && pct_decode(colon + 1, (size_t)(at - colon - 1), x->proxy_pass, sizeof x->proxy_pass) < 0)
            return WTLS_ERR_ARG;
        x->proxy_auth = true;
    }
    size_t hl = (size_t)(auth_end - hostport);
    char tmp[300];
    if (hl == 0 || hl + 10 > sizeof tmp) return WTLS_ERR_ARG;
    memcpy(tmp, "http://", 7);
    memcpy(tmp + 7, hostport, hl);
    tmp[7 + hl] = '/';
    tmp[8 + hl] = '\0';
    wtls_url pu;
    if (wtls_url_parse(tmp, &pu) != WTLS_OK || pu.secure) return WTLS_ERR_ARG;
    bool explicit_port = memchr(hostport, ':', hl) != NULL;
    memcpy(x->proxy_host, pu.host, sizeof x->proxy_host);
    x->proxy_port = explicit_port ? pu.port : default_port;
    x->proxy_kind = kind;
    x->has_proxy = true;
    if (kind == PROXY_HTTP && x->proxy_auth) {
        if (wtls_http_basic_auth(x->proxy_user, x->proxy_pass, x->proxy_basic, sizeof x->proxy_basic, NULL) != WTLS_OK)
            return WTLS_ERR_ARG;
    } else if (x->proxy_auth && (strlen(x->proxy_user) > 255 || strlen(x->proxy_pass) > 255)) {
        return WTLS_ERR_ARG;
    }
    return WTLS_OK;
}

static int tr_read_exact(wtls_http_response *r, void *buf, size_t len) {
    uint8_t *p = buf;
    while (len) {
        int k = tr_read(r, p, len);
        if (k <= 0) return k == 0 || k == WTLS_ERR_CLOSED ? WTLS_ERR_IO : k;
        p += k;
        len -= (size_t)k;
    }
    return WTLS_OK;
}

/* RFC 1928 CONNECT (and RFC 1929 username/password) over r->conn. */
static int socks5_connect(wtls_http_response *r, request_ctx *x) {
    http_tr *t = r->conn;
    uint8_t buf[300];
    size_t n = 0;
    buf[n++] = 5;
    if (x->proxy_auth) { buf[n++] = 2; buf[n++] = 0; buf[n++] = 2; }
    else { buf[n++] = 1; buf[n++] = 0; }
    int st = tr_write_all(t, buf, n);
    if (st == WTLS_OK) st = tr_read_exact(r, buf, 2);
    if (st != WTLS_OK) return st;
    if (buf[0] != 5 || buf[1] == 0xFF) return WTLS_ERR_CONNECT;
    if (buf[1] == 2) {
        size_t ul = strlen(x->proxy_user), pl = strlen(x->proxy_pass);
        if (!x->proxy_auth || ul > 255 || pl > 255) return WTLS_ERR_CONNECT;
        uint8_t msg[3 + 255 + 255];
        size_t m = 0;
        msg[m++] = 1;
        msg[m++] = (uint8_t)ul;
        memcpy(msg + m, x->proxy_user, ul);
        m += ul;
        msg[m++] = (uint8_t)pl;
        memcpy(msg + m, x->proxy_pass, pl);
        m += pl;
        st = tr_write_all(t, msg, m);
        if (st == WTLS_OK) st = tr_read_exact(r, buf, 2);
        if (st != WTLS_OK) return st;
        if (buf[1] != 0) return WTLS_ERR_CONNECT;
    } else if (buf[1] != 0) {
        return WTLS_ERR_CONNECT;
    }
    /* the request: names go to the proxy for socks5h, otherwise we resolve (IPv4) ourselves */
    n = 0;
    buf[n++] = 5;
    buf[n++] = 1;
    buf[n++] = 0;
    uint32_t ip = 0;
    bool by_name = x->proxy_kind == PROXY_SOCKS5H && !wtls_plat_parse_ipv4(r->host, &ip);
    if (!by_name && !wtls_plat_parse_ipv4(r->host, &ip)) {
        int rs = wtls_dns_lookup(r->host, &ip, wtls_plat_now_ms());
        if (rs != WTLS_OK) return rs;
    }
    if (by_name) {
        size_t hl = strlen(r->host);
        if (hl == 0 || hl > 255) return WTLS_ERR_ARG;
        buf[n++] = 3;
        buf[n++] = (uint8_t)hl;
        memcpy(buf + n, r->host, hl);
        n += hl;
    } else {
        buf[n++] = 1;
        memcpy(buf + n, &ip, 4); /* already in network order */
        n += 4;
    }
    buf[n++] = (uint8_t)(r->port >> 8);
    buf[n++] = (uint8_t)r->port;
    st = tr_write_all(t, buf, n);
    if (st == WTLS_OK) st = tr_read_exact(r, buf, 4);
    if (st != WTLS_OK) return st;
    if (buf[0] != 5 || buf[1] != 0) return WTLS_ERR_CONNECT;
    size_t rest;
    if (buf[3] == 1) rest = 4 + 2;
    else if (buf[3] == 4) rest = 16 + 2;
    else if (buf[3] == 3) {
        st = tr_read_exact(r, buf, 1);
        if (st != WTLS_OK) return st;
        rest = (size_t)buf[0] + 2;
    } else return WTLS_ERR_CONNECT;
    while (rest) { /* the bound address is of no use to us */
        size_t k = rest < sizeof buf ? rest : sizeof buf;
        st = tr_read_exact(r, buf, k);
        if (st != WTLS_OK) return st;
        rest -= k;
    }
    return WTLS_OK;
}

static int connect_proxy_tunnel(wtls_http_response *r, request_ctx *x) {
    http_tr *t = wtls_calloc(1, sizeof *t);
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
        wtls_free(t);
        return st;
    }
    if (x->proxy_kind == PROXY_HTTP && !r->secure) {
        r->conn = t;
        return WTLS_OK;
    }
    r->conn = t;
    if (x->proxy_kind != PROXY_HTTP) {
        st = socks5_connect(r, x);
        if (st != WTLS_OK) {
            wtls_http_tr_close(t);
            r->conn = NULL;
            return st == WTLS_ERR_IO ? WTLS_ERR_CONNECT : st;
        }
        if (!r->secure) return WTLS_OK;
    } else {
        char creq[1100];
        int n = snprintf(creq, sizeof creq, "CONNECT %s:%u HTTP/1.1\r\nHost: %s:%u\r\n%s%s%s\r\n", r->host,
                         (unsigned)r->port, r->host, (unsigned)r->port,
                         x->proxy_auth ? "Proxy-Authorization: " : "", x->proxy_auth ? x->proxy_basic : "",
                         x->proxy_auth ? "\r\n" : "");
        if (n <= 0 || (size_t)n >= sizeof creq) {
            wtls_http_tr_close(t);
            r->conn = NULL;
            return WTLS_ERR_ARG;
        }
        st = tr_write_all(t, creq, (size_t)n);
        if (st == WTLS_OK) {
            bool got = false;
            size_t cap = 0, fill = 0;
            uint32_t hdr_t = x->header_timeout;
            st = read_head_ex(r, hdr_t, hdr_t, false, false, &cap, &fill, &got);
            if (st == WTLS_OK && (r->head.status < 200 || r->head.status >= 300 || r->extra_end > r->extra_off))
                st = WTLS_ERR_CONNECT;
            wtls_free(r->buf);
            r->buf = NULL;
            r->extra_off = r->extra_end = 0;
        }
        if (st != WTLS_OK) {
            wtls_http_tr_close(t);
            r->conn = NULL;
            return st;
        }
    }
    int fd = t->fd;
    t->fd = -1;
    st = wtls_connect_on_fd(&t->tls, fd, r->host, r->port, &x->opt);
    if (st != WTLS_OK) {
        wtls_free(t);
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


#if WTLS_HTTP2
/* ---- multiplexed HTTP/2 sessions of the asynchronous API (docs/http2-multiplexing-design.md) --------------- */

#define H2_SESSION_IDLE_MS 60000u

struct h2_sess {
    h2_sess *next;
    http_tr *tr; /* owns the TLS connection; non-blocking */
    h2s_session *s;
    char host[256];
    uint16_t port;
    bool insecure, has_cred;
    uint8_t cred_fp[WTLS_HTTP_POOL_FP_LEN];
    wtls_conn_info info;
    unsigned refs;       /* responses holding a stream of it */
    uint64_t idle_since; /* ms, 0 while in use */
    bool used;           /* has carried a request */
    bool retired;        /* wtls_http_pool_flush(): no new streams, closed with its last one */
};

/* A first connection to a host that is still negotiating its protocol: later requests wait for the outcome. */
typedef struct h2_pend {
    struct h2_pend *next;
    const void *owner;
    char host[256];
    uint16_t port;
    bool insecure, has_cred;
    uint8_t cred_fp[WTLS_HTTP_POOL_FP_LEN];
} h2_pend;

static h2_sess *g_h2_sessions;
static h2_pend *g_h2_pend;

static h2s_session *h2_session_of(const wtls_http_response *r) { return r->h2sess->s; }

static int h2_tr_read(void *c, void *buf, size_t len) {
    h2_sess *hs = c;
    return wtls_read(hs->tr->tls, buf, len);
}

static int h2_tr_write(void *c, const void *buf, size_t len) {
    h2_sess *hs = c;
    return wtls_write(hs->tr->tls, buf, len);
}

static const h2s_io k_h2_tr_io = {h2_tr_read, h2_tr_write};

static void h2_sess_free(h2_sess *hs) {
    for (h2_sess **pp = &g_h2_sessions; *pp; pp = &(*pp)->next)
        if (*pp == hs) {
            *pp = hs->next;
            break;
        }
    h2s_destroy(hs->s);
    wtls_http_tr_close(hs->tr);
    wtls_free(hs);
}

/* Frees what nobody uses and will not be used again: dead, retired or idle for too long. */
static void h2_sess_gc(uint64_t now) {
    h2_sess *hs = g_h2_sessions;
    while (hs) {
        h2_sess *next = hs->next;
        if (hs->refs == 0 &&
            (hs->retired || !h2s_usable(hs->s) || (hs->idle_since && now - hs->idle_since > H2_SESSION_IDLE_MS)))
            h2_sess_free(hs);
        hs = next;
    }
}

static void h2_sess_unref(h2_sess *hs) {
    if (hs->refs) hs->refs--;
    if (hs->refs == 0) {
        hs->idle_since = wtls_plat_now_ms();
        h2_sess_gc(hs->idle_since);
    }
}

/* The response lets go of its stream (resetting it if unfinished) and of the session. */
static void h2_detach(wtls_http_response *r) {
    if (r->h2st) {
        h2s_stream_release(r->h2st);
        r->h2st = NULL;
        if (r->h2sess) h2s_pump(r->h2sess->s); /* sends the RST_STREAM and credit now */
    }
    if (r->h2sess) {
        h2_sess *hs = r->h2sess;
        r->h2sess = NULL;
        h2_sess_unref(hs);
    }
}

static bool h2_key_match(const wtls_http_response *r, const char *host, uint16_t port, bool insecure, bool has_cred,
                         const uint8_t *fp) {
    return port == r->port && insecure == r->insecure && has_cred == r->has_cred && strcmp(host, r->host) == 0 &&
           (!has_cred || memcmp(fp, r->cred_fp, WTLS_HTTP_POOL_FP_LEN) == 0);
}

/* A live session for r's key with a free stream. */
static h2_sess *h2_sess_find(const wtls_http_response *r) {
    for (h2_sess *hs = g_h2_sessions; hs; hs = hs->next)
        if (!hs->retired && h2s_usable(hs->s) && h2s_active(hs->s) < h2s_capacity(hs->s) &&
            h2_key_match(r, hs->host, hs->port, hs->insecure, hs->has_cred, hs->cred_fp))
            return hs;
    return NULL;
}

/* Something is still being found out about this host: a connection negotiating its protocol, or a new session
 * whose stream limit is not known yet. Requests wait for it instead of opening connections of their own. */
static bool h2_pend_find(const wtls_http_response *r) {
    for (h2_pend *p = g_h2_pend; p; p = p->next)
        if (h2_key_match(r, p->host, p->port, p->insecure, p->has_cred, p->cred_fp)) return true;
    for (h2_sess *hs = g_h2_sessions; hs; hs = hs->next)
        if (!hs->retired && h2s_usable(hs->s) && !h2s_settings_known(hs->s) &&
            h2_key_match(r, hs->host, hs->port, hs->insecure, hs->has_cred, hs->cred_fp))
            return true;
    return false;
}

/* A live session for the key exists but all its streams are busy. */
static bool h2_full_find(const wtls_http_response *r) {
    for (h2_sess *hs = g_h2_sessions; hs; hs = hs->next)
        if (!hs->retired && h2s_usable(hs->s) &&
            h2_key_match(r, hs->host, hs->port, hs->insecure, hs->has_cred, hs->cred_fp))
            return true;
    return false;
}

static int h2_pend_add(const wtls_http_response *r, const void *owner) {
    h2_pend *p = wtls_calloc(1, sizeof *p);
    if (!p) return WTLS_ERR_NOMEM;
    p->owner = owner;
    memcpy(p->host, r->host, sizeof p->host);
    p->port = r->port;
    p->insecure = r->insecure;
    p->has_cred = r->has_cred;
    memcpy(p->cred_fp, r->cred_fp, sizeof p->cred_fp);
    p->next = g_h2_pend;
    g_h2_pend = p;
    return WTLS_OK;
}

static void h2_pend_remove(const void *owner) {
    for (h2_pend **pp = &g_h2_pend; *pp;) {
        if ((*pp)->owner == owner) {
            h2_pend *dead = *pp;
            *pp = dead->next;
            wtls_free(dead);
        } else {
            pp = &(*pp)->next;
        }
    }
}

/* A session on the negotiated connection of r; takes tr. NULL: out of memory (tr is closed). */
static h2_sess *h2_sess_create(const wtls_http_response *r, http_tr *tr) {
    h2_sess *hs = wtls_calloc(1, sizeof *hs);
    if (!hs) {
        wtls_http_tr_close(tr);
        return NULL;
    }
    hs->tr = tr;
    hs->s = h2s_create(&k_h2_tr_io, hs, WTLS_H2_MAX_STREAMS, WTLS_H2_STREAM_WINDOW);
    if (!hs->s) {
        wtls_http_tr_close(tr);
        wtls_free(hs);
        return NULL;
    }
    memcpy(hs->host, r->host, sizeof hs->host);
    hs->port = r->port;
    hs->insecure = r->insecure;
    hs->has_cred = r->has_cred;
    memcpy(hs->cred_fp, r->cred_fp, sizeof hs->cred_fp);
    if (wtls_get_info(tr->tls, &hs->info) != WTLS_OK) memset(&hs->info, 0, sizeof hs->info);
    tr_set_abort(tr, NULL, NULL); /* the connection outlives the request that opened it */
    hs->next = g_h2_sessions;
    g_h2_sessions = hs;
    wtls_shutdown_hook = wtls_http_pool_flush;
    wtls_pool_flush_hook = wtls_http_pool_flush;
    return hs;
}

/* wtls_http_pool_flush(): idle sessions close now, busy ones with their last response. */
void wtls_http_h2s_flush(void) {
    for (h2_sess *hs = g_h2_sessions; hs; hs = hs->next) hs->retired = true;
    h2_sess_gc(wtls_plat_now_ms());
}
#endif

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
    uint8_t *buf = wtls_malloc(UPLOAD_BUF_SIZE);
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
    wtls_free(buf);
    return ret;
}

static int exchange_h2(wtls_http_response *r, request_ctx *x, bool *got) {
    wtls_h2_conn *h2 = r->conn->h2;
    /* An earlier hop (redirect) leaves its head in r->buf; the HPACK decoder
     * hands out a new arena and would overwrite the pointer. */
    wtls_free(r->buf);
    r->buf = NULL;
    memset(&r->head, 0, sizeof r->head); /* its fields pointed into r->buf */
    uint32_t io_timeout = tr_timeout(r->conn);
    uint32_t t = x->header_timeout;
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
    uint32_t t = x->header_timeout;
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
#if WTLS_HTTP2
static void h2_detach(wtls_http_response *r);
#endif

static void release_conn(wtls_http_response *r) {
#if WTLS_HTTP2
    h2_detach(r);
#endif
    if (!r->conn) return;
    if (r->conn->nonblock) { /* pooled connections are blocking ones */
        r->conn->nonblock = false;
        if (r->conn->tls) wtls_set_nonblocking(r->conn->tls, false);
    }
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
static int frame_h2(wtls_http_response *r, bool reusable) {
    memset(&r->fr, 0, sizeof r->fr);
    r->fr.reusable = reusable;
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

/* First half of a hop: builds the request head for *u into x->head (the caller frees it) and fills the
 * per-hop fields of r (scheme, port, host, pool identity) and resets its read state. */
static int hop_begin(wtls_http_response *r, request_ctx *x, const wtls_url *u) {
    char *head = NULL;
    unsigned req_flags = (x->body_chunked ? WTLS_HTTP_REQ_CHUNKED : 0u) |
                         (x->req->no_keepalive ? WTLS_HTTP_REQ_NO_KEEPALIVE : 0u) |
                         (x->want_gzip ? WTLS_HTTP_REQ_GZIP : 0u) |
                         (x->allow_upgrade ? WTLS_HTTP_REQ_UPGRADE : 0u) |
                         (x->expect_100 ? WTLS_HTTP_REQ_EXPECT_100 : 0u) |
                         (x->proxy_kind == PROXY_HTTP ? WTLS_HTTP_REQ_PROXY : 0u);
    const wtls_http_header *send_h = x->headers;
    size_t send_n = x->n_headers;
    wtls_http_header *with_pa = NULL;
    if (x->proxy_kind == PROXY_HTTP && x->proxy_auth && !u->secure && !wtls_http_headers_have(x->headers, x->n_headers, "Proxy-Authorization")) {
        with_pa = wtls_malloc((x->n_headers + 1) * sizeof *with_pa);
        if (!with_pa) return WTLS_ERR_NOMEM;
        if (x->n_headers) memcpy(with_pa, x->headers, x->n_headers * sizeof *with_pa);
        with_pa[x->n_headers] = (wtls_http_header){"Proxy-Authorization", x->proxy_basic};
        send_h = with_pa;
        send_n = x->n_headers + 1;
    }
    int st = wtls_http_build_head(x->method, u, send_h, send_n, x->body != NULL || x->body_cb != NULL,
                                  x->body_len, req_flags, &head, &x->head_len);
    wtls_free(with_pa);
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
            wtls_free(head);
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
    WTLS_LOG(WTLS_LOG_DEBUG, WTLS_LOG_CAT_HTTP, "%s %s://%s:%u", x->method, u->secure ? "https" : "http",
             u->host, (unsigned)u->port);
    return WTLS_OK;
}

/* Second half: the final head is in r->head; works out how the body is framed. */
static int hop_frame(wtls_http_response *r, request_ctx *x) {
    int st;
#if WTLS_HTTP2
    if (r->h2st || r->conn->h2) {
        r->is_http2 = true;
        st = frame_h2(r, !r->h2st && !x->req->no_keepalive && !x->early_final && !x->has_proxy &&
                             wtls_h2_reusable(r->conn->h2));
        if (st != WTLS_OK) return st;
        r->remaining = r->fr.length;
        r->eof = r->fr.kind == WTLS_BODY_NONE || (r->fr.kind == WTLS_BODY_LENGTH && r->fr.length == 0);
        if (r->h2st && r->eof && h2s_stream_ended(r->h2st)) h2_detach(r); /* nothing more to read: free the slot */
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

/* One request/response exchange against *u: head built, connection acquired
 * (pooled or new), final head read and framed. */
static int hop(wtls_http_response *r, request_ctx *x, const wtls_url *u) {
    int st = hop_begin(r, x, u);
    if (st != WTLS_OK) return st;
    char *head = (char *)x->head; /* hop_begin allocated it */
    bool got = false;
    st = acquire(r, x);
    if (st == WTLS_OK) {
        WTLS_LOG(WTLS_LOG_DEBUG, WTLS_LOG_CAT_HTTP, "%s:%u: %s connection", u->host, (unsigned)u->port,
                 r->reused ? "pooled" : "new");
        st = exchange(r, x, &got);
    }
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
    wtls_free(head);
    x->head = NULL;
    if (st != WTLS_OK) return st;
    return hop_frame(r, x);
}

/* Path of the request target without the query ("/" for none). */
static void request_path(const wtls_url *u, char out[1025]) {
    if (u->target_needs_slash || u->target_len == 0 || u->target[0] != '/') {
        strcpy(out, "/");
        return;
    }
    size_t n = 0;
    while (n < u->target_len && n < 1024 && u->target[n] != '?') {
        out[n] = u->target[n];
        n++;
    }
    out[n] = '\0';
}

static bool has_cookie_header(const wtls_http_header *h, size_t n) {
    for (size_t i = 0; i < n; i++) {
        const char *a = h[i].name, *b = "cookie";
        while (*a && *b && ((*a | 0x20) == *b)) {
            a++;
            b++;
        }
        if (!*a && !*b) return true;
    }
    return false;
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
        wtls_free(next);
        return WTLS_ERR_HTTP_REDIRECT; /* bad Location, or https -> http */
    }

    WTLS_LOG(WTLS_LOG_INFO, WTLS_LOG_CAT_HTTP, "redirect %d to %s://%s:%u", r->redirects + 1,
             nu.secure ? "https" : "http", nu.host, (unsigned)nu.port);
    if (!*filtered && !wtls_url_same_origin(u, &nu)) {
        /* The next hop leaves the origin: strip credentials, both the
         * header credentials and the client certificate, which must never
         * be presented to a different origin. */
        x->opt.client_cred = NULL;
        if (x->n_headers) {
            wtls_http_header *h = wtls_malloc(x->n_headers * sizeof *h);
            if (!h) {
                wtls_free(next);
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
            wtls_free(next);
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
        wtls_free(next);
        return WTLS_ERR_CANCELLED;
    }
    release_conn(r);
    wtls_free(r->url);
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
#if WTLS_CODECS
    if (!want_gzip || r->fr.kind == WTLS_BODY_NONE) return WTLS_OK;
    const char *ce = wtls_http_head_get(&r->head, "content-encoding");
    if (!ce || !*ce || wtls_http_ieq(ce, "identity")) return WTLS_OK;
    if (false) {
#if WTLS_GZIP
    } else if (wtls_http_ieq(ce, "gzip") || wtls_http_ieq(ce, "x-gzip")) {
        r->gz = wtls_gzip_new();
        r->is_deflate = false;
        r->enc_kind = 1;
    } else if (wtls_http_ieq(ce, "deflate")) {
        r->gz = wtls_deflate_new();
        r->is_deflate = true;
        r->enc_kind = 2;
#endif
#if WTLS_BROTLI
    } else if (wtls_http_ieq(ce, "br")) {
        r->gz = wtls_br_new();
        r->enc_kind = 3;
#endif
#if WTLS_ZSTD
    } else if (wtls_http_ieq(ce, "zstd")) {
        r->gz = wtls_zstd_new();
        r->enc_kind = 4;
#endif
    } else {
        return WTLS_ERR_HTTP;
    }
    r->gz_in = wtls_malloc(GZ_IN_BUF);
    if (!r->gz || !r->gz_in) return WTLS_ERR_NOMEM; /* response_free() frees what was made */
#else
    (void)r;
    (void)want_gzip;
#endif
    return WTLS_OK;
}

/* The configured stage timeouts (0 resolved to defaults); the total deadline cuts them per hop. */
typedef struct {
    uint32_t connect, handshake, io, header;
} base_timeouts;

/* Shared by wtls_http_open and wtls_http_start: validates req, allocates the response (*rp), parses the URL into *u
 * and fills x. On an error *rp is NULL. async: refuses what the non-blocking path cannot do (yet). */
static int request_setup(const wtls_http_request *req, bool async, wtls_http_response **rp, request_ctx *x,
                         wtls_url *u, base_timeouts *bt, uint64_t deadline_ms) {
    *rp = NULL;
    if (!req || !req->url) return WTLS_ERR_ARG;
    if (!wtls_core_initialized()) return WTLS_ERR_STATE;
    if (wtls_plat_net_changed()) wtls_net_reset();
    if (req->n_headers && !req->headers) return WTLS_ERR_ARG;
    if (req->body_len && !req->body && !req->body_cb) return WTLS_ERR_ARG;
    if (req->body && req->body_cb) return WTLS_ERR_ARG;
    if (req->body_chunked && !req->body_cb) return WTLS_ERR_ARG;
    if (async && (req->proxy_url || req->expect_100_continue || req->allow_upgrade)) return WTLS_ERR_UNSUPPORTED;

    wtls_http_response *r = wtls_calloc(1, sizeof *r);
    if (!r) return WTLS_ERR_NOMEM;
    size_t url_len = strnlen(req->url, WTLS_HTTP_MAX_URL + 1);
    if (url_len > WTLS_HTTP_MAX_URL) {
        wtls_free(r);
        return WTLS_ERR_ARG;
    }
    r->url = wtls_malloc(url_len + 1);
    if (!r->url) {
        wtls_free(r);
        return WTLS_ERR_NOMEM;
    }
    memcpy(r->url, req->url, url_len + 1);

    int st = wtls_url_parse(r->url, u);
    if (st != WTLS_OK) {
        response_free(r);
        return st;
    }
    memset(x, 0, sizeof *x);
    x->req = req;
    x->method = req->method ? req->method : "GET";
    x->body = req->body;
    x->body_len = req->body_len;
    x->body_cb = req->body_cb;
    x->body_ctx = req->body_ctx;
    x->body_chunked = req->body_chunked;
    x->headers = req->headers;
    x->n_headers = req->n_headers;
    x->allow_upgrade = req->allow_upgrade;
    x->expect_100 = req->expect_100_continue && (req->body_cb != NULL || (req->body != NULL && req->body_len > 0));
#if WTLS_CODECS
    /* An application that sets Accept-Encoding itself gets the body as sent. */
    x->want_gzip = !req->allow_upgrade && !req->no_gzip &&
                   !wtls_http_headers_have(req->headers, req->n_headers, "Accept-Encoding");
#endif
    if (req->proxy_url) {
        size_t plen = strnlen(req->proxy_url, WTLS_HTTP_MAX_URL + 1);
        if (plen > WTLS_HTTP_MAX_URL) {
            response_free(r);
            return WTLS_ERR_ARG;
        }
        int pst = parse_proxy(req->proxy_url, x);
        if (pst != WTLS_OK) {
            response_free(r);
            return pst;
        }
    }
    if (req->tls) x->opt = *req->tls;
    uint8_t pin_check[WTLS_PINS_TOTAL][WTLS_SPKI_SHA256_LEN];
    uint8_t n_pin_check = 0;
    if (u->secure && wtls_pins_collect(&x->opt, pin_check, &n_pin_check) != WTLS_OK) {
        response_free(r);
        return WTLS_ERR_ARG;
    }
#if WTLS_HTTP2
    x->want_h2 = !req->no_h2 && !req->allow_upgrade;
    x->opt.alpn = x->want_h2 ? k_alpn_h2 : k_alpn_h1;
#else
    x->opt.alpn = k_alpn_h1;
#endif
    r->insecure = x->opt.insecure_skip_verify;
    r->abort_cb = x->opt.abort_cb;
    r->abort_ctx = x->opt.abort_ctx;
    if (r->abort_cb && r->abort_cb(r->abort_ctx)) {
        response_free(r);
        return WTLS_ERR_CANCELLED;
    }
    x->header_timeout = req->header_timeout_ms ? req->header_timeout_ms : WTLS_HTTP_DEFAULT_HEADER_TIMEOUT_MS;
    bt->connect = x->opt.connect_timeout_ms ? x->opt.connect_timeout_ms : DEF_CONNECT_MS;
    bt->handshake = x->opt.handshake_timeout_ms ? x->opt.handshake_timeout_ms : DEF_HANDSHAKE_MS;
    bt->io = x->opt.io_timeout_ms ? x->opt.io_timeout_ms : DEF_IO_MS;
    bt->header = x->header_timeout;
    r->deadline_ms = deadline_ms;
    *rp = r;
    return WTLS_OK;
}

/* total_timeout_ms: cuts the stage timeouts of the next hop to what is left. WTLS_ERR_TIMEOUT once nothing is. */
static int apply_deadline(request_ctx *x, const base_timeouts *bt, uint64_t deadline_ms) {
    if (!deadline_ms) return WTLS_OK;
    uint64_t now = wtls_plat_now_ms();
    if (now >= deadline_ms) return WTLS_ERR_TIMEOUT;
    uint32_t left = deadline_ms - now > UINT32_MAX ? UINT32_MAX : (uint32_t)(deadline_ms - now);
    x->opt.connect_timeout_ms = bt->connect < left ? bt->connect : left;
    x->opt.handshake_timeout_ms = bt->handshake < left ? bt->handshake : left;
    x->opt.io_timeout_ms = bt->io < left ? bt->io : left;
    x->header_timeout = bt->header < left ? bt->header : left;
    return WTLS_OK;
}

/* Cookie jar, one hop: the Cookie header for *u while the hop's head is built, and the Set-Cookie fields after. */
typedef struct {
    char path[1025];
    char *value;
    wtls_http_header *hdrs;
    const wtls_http_header *saved_h;
    size_t saved_n;
    bool active; /* the headers of x carry the Cookie field until cookie_hop_end() */
} cookie_hop;

static void cookie_hop_begin(const wtls_http_request *req, request_ctx *x, const wtls_url *u, cookie_hop *c) {
    c->active = true;
    c->value = NULL;
    c->hdrs = NULL;
    c->saved_h = x->headers;
    c->saved_n = x->n_headers;
    c->path[0] = '\0';
    if (!req->cookie_jar) return;
    request_path(u, c->path);
    if (has_cookie_header(x->headers, x->n_headers)) return;
    c->value = wtls_cookie_jar_header(req->cookie_jar, u->host, c->path, u->secure, wtls_plat_unix_time());
    if (!c->value) return;
    c->hdrs = wtls_malloc((x->n_headers + 1) * sizeof *c->hdrs);
    if (!c->hdrs) return;
    if (x->n_headers) memcpy(c->hdrs, x->headers, x->n_headers * sizeof *c->hdrs);
    c->hdrs[x->n_headers] = (wtls_http_header){"Cookie", c->value};
    x->headers = c->hdrs;
    x->n_headers++;
}

static void cookie_hop_end(request_ctx *x, cookie_hop *c) {
    if (!c->active) return;
    c->active = false;
    x->headers = c->saved_h;
    x->n_headers = c->saved_n;
    wtls_free(c->hdrs);
    wtls_free(c->value);
    c->hdrs = NULL;
    c->value = NULL;
}

static void cookie_hop_store(const wtls_http_request *req, const wtls_http_response *r, const wtls_url *u,
                             const cookie_hop *c) {
    if (!req->cookie_jar) return;
    int64_t now = wtls_plat_unix_time();
    for (size_t i = 0; i < r->head.n_fields; i++)
        if (strcmp(r->head.fields[i].name, "set-cookie") == 0)
            (void)wtls_cookie_jar_set(req->cookie_jar, r->head.fields[i].value, u->host, c->path, u->secure, now);
}

/* What both entry points do once the final head is in: decoding, progress, the body deadline. */
static int response_ready(wtls_http_response *r, const request_ctx *x, const wtls_http_request *req) {
    int st = setup_encoding(r, x->want_gzip);
    if (st != WTLS_OK) return st;
    r->progress_cb = req->progress_cb;
    r->progress_ctx = req->progress_ctx;
    r->progress_done = 0;
    r->progress_total = wtls_http_content_length(r);
    return WTLS_OK;
}

static wtls_status open_attempt(wtls_http_response **out, const wtls_http_request *req, uint64_t deadline_ms) {
    if (!out) return WTLS_ERR_ARG;
    *out = NULL;
    if (!req || !req->url) return WTLS_ERR_ARG;
    if (!wtls_core_initialized()) return WTLS_ERR_STATE;
    if (!WTLS_THREAD_GUARD("wtls_http_open")) return WTLS_ERR_STATE;
    wtls_http_response *r;
    request_ctx x;
    wtls_url u;
    base_timeouts bt;
    int st = request_setup(req, false, &r, &x, &u, &bt, deadline_ms);
    if (st != WTLS_OK) return st;

    wtls_http_header *filtered = NULL;
    for (;;) {
        cookie_hop ck;
        cookie_hop_begin(req, &x, &u, &ck);
        st = apply_deadline(&x, &bt, deadline_ms);
        if (st == WTLS_OK) st = hop(r, &x, &u);
        cookie_hop_end(&x, &ck);
        if (st != WTLS_OK) break;
        cookie_hop_store(req, r, &u, &ck);
        const char *location = wtls_http_head_get(&r->head, "location");
        if (req->max_redirects < 0 || !is_redirect(r->head.status) || !location) break;
        st = follow(r, &x, &u, location, &filtered);
        if (st != WTLS_OK) break;
    }
    wtls_free(filtered);
    if (st == WTLS_OK) st = response_ready(r, &x, req);
    if (st != WTLS_OK) {
        response_free(r);
        return st;
    }
    *out = r;
    return WTLS_OK;
}

/* ---- retries and the total deadline --------------------------------------------- */

static bool method_retryable(const char *m) { return wtls_http_method_idempotent(m ? m : "GET"); }

/* Sleeps ms in slices so abort_cb stays responsive; WTLS_OK or WTLS_ERR_CANCELLED. */
static int retry_sleep(uint32_t ms, const wtls_http_request *req) {
    uint64_t end = wtls_plat_now_ms() + ms;
    for (;;) {
        if (req->tls && req->tls->abort_cb && req->tls->abort_cb(req->tls->abort_ctx)) return WTLS_ERR_CANCELLED;
        uint64_t now = wtls_plat_now_ms();
        if (now >= end) return WTLS_OK;
        uint64_t left = end - now;
        wtls_plat_sleep_ms(left > 50 ? 50u : (uint32_t)left);
    }
}

static uint32_t backoff_for(uint32_t base, unsigned attempt) {
    uint64_t v = (uint64_t)base << (attempt > 16 ? 16 : attempt);
    if (v > 30000u) v = 30000u;
    uint32_t jitter_permille = 750u + (uint32_t)(wtls_plat_now_us() % 501u); /* 75 % .. 125 % */
    return (uint32_t)(v * jitter_permille / 1000u);
}

wtls_status wtls_http_open(wtls_http_response **out, const wtls_http_request *req) {
    if (!out) return WTLS_ERR_ARG;
    *out = NULL;
    if (!req) return WTLS_ERR_ARG;
    uint64_t deadline = req->total_timeout_ms ? wtls_plat_now_ms() + req->total_timeout_ms : 0;
    if (req->total_timeout_ms == 0 && req->max_retries == 0) return open_attempt(out, req, 0); /* the 1.0 path */
    const bool can_retry = req->max_retries > 0 && !req->body_cb && method_retryable(req->method);
    const uint32_t base = req->retry_backoff_ms ? req->retry_backoff_ms : 500u;
    for (unsigned attempt = 0;; attempt++) {
        wtls_status st = open_attempt(out, req, deadline);
        uint32_t wait = 0;
        bool retry = false, on_status = false;
        if (st == WTLS_OK) {
            int code = wtls_http_status(*out);
            if (can_retry && req->retry_on_503 && attempt < req->max_retries && (code == 429 || code == 503)) {
                const char *ra = wtls_http_get_header(*out, "retry-after");
                uint32_t secs = 0;
                bool digits = ra && *ra;
                for (const char *c = ra; digits && *c; c++) {
                    if (*c < '0' || *c > '9' || secs > 60) digits = false;
                    else secs = secs * 10 + (uint32_t)(*c - '0');
                }
                wait = digits ? (secs > 60 ? 60u : secs) * 1000u : backoff_for(base, attempt);
                retry = on_status = true;
            }
        } else if (can_retry && attempt < req->max_retries &&
                   (st == WTLS_ERR_DNS || st == WTLS_ERR_CONNECT || st == WTLS_ERR_TIMEOUT || st == WTLS_ERR_IO)) {
            wait = backoff_for(base, attempt);
            retry = true;
        }
        if (!retry) return st;
        if (deadline) {
            uint64_t now = wtls_plat_now_ms();
            if (now >= deadline || deadline - now <= wait) {
                if (on_status) return st; /* no time for another try: the answer we have is the result */
                return WTLS_ERR_TIMEOUT;
            }
        }
        if (on_status) {
            wtls_http_close(*out);
            *out = NULL;
        }
        int sl = retry_sleep(wait, req);
        if (sl != WTLS_OK) return sl;
    }
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
#if WTLS_CODECS
    out->gzip = r->enc_kind == 1;
    out->deflate = r->enc_kind == 2;
    out->br = r->enc_kind == 3;
    out->zstd = r->enc_kind == 4;
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

/* A non-blocking transport has nothing for us right now: not an error, not sticky. */
static bool is_want(int k) { return k == WTLS_WANT_READ || k == WTLS_WANT_WRITE; }

/* The body as framed on the wire (len >= 1, <= INT_MAX). */
static int raw_read(wtls_http_response *r, void *buf, size_t len) {
    if (r->eof) return 0;
#if WTLS_HTTP2
    if (r->h2st) {
        h2s_pump(h2_session_of(r));
        bool stream_eof = false;
        int k = h2s_stream_read(r->h2st, buf, len, &stream_eof);
        if (is_want(k)) return k;
        if (k < 0) return fail(r, k);
        if (r->fr.kind == WTLS_BODY_LENGTH) {
            if ((uint64_t)k > r->remaining) return fail(r, WTLS_ERR_IO);
            r->remaining -= (uint64_t)k;
            if (stream_eof && r->remaining > 0) return fail(r, WTLS_ERR_IO);
            if (!r->remaining) r->eof = true;
        } else if (stream_eof) {
            r->eof = true;
        }
        if (r->eof && h2s_stream_ended(r->h2st)) h2_detach(r); /* the stream's slot is free again */
        return k;
    }
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
        if (is_want(k)) return k;
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
        if (is_want(k)) return k;
        return k < 0 ? fail(r, k) : k;
    }
    case WTLS_BODY_CHUNKED:
        for (;;) {
            int k = pull(r, buf, len);
            if (is_want(k)) return k;
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

#if WTLS_CODECS
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
    if (r->as) return WTLS_ERR_STATE; /* wtls_http_step() has not finished the head yet */
    if (r->err) return r->err;
    if (r->abort_cb && r->abort_cb(r->abort_ctx)) {
        tr_mark_aborted(r->conn);
        return fail(r, WTLS_ERR_CANCELLED);
    }
    if (!len) return 0;
    if (len > INT_MAX) len = INT_MAX;
    uint32_t saved_to = 0;
    bool clamped = false;
    bool nonblock = r->conn && r->conn->nonblock;
#if WTLS_HTTP2
    if (r->h2st) {
        nonblock = true;
        if (r->deadline_ms && !r->eof && wtls_plat_now_ms() >= r->deadline_ms) return fail(r, WTLS_ERR_TIMEOUT);
    }
#endif
    if (r->deadline_ms && !r->eof && r->conn) { /* total_timeout_ms covers the body too */
        uint64_t now = wtls_plat_now_ms();
        if (now >= r->deadline_ms) return fail(r, WTLS_ERR_TIMEOUT);
        uint64_t left = r->deadline_ms - now;
        saved_to = tr_timeout(r->conn);
        if (left < saved_to) {
            tr_set_timeout(r->conn, (uint32_t)left);
            clamped = true;
        }
    }
    int k;
#if WTLS_CODECS
    k = r->gz ? gz_read(r, buf, len) : raw_read(r, buf, len);
#else
    k = raw_read(r, buf, len);
#endif
    if (clamped && r->conn) tr_set_timeout(r->conn, saved_to);
    if (nonblock) {
        uint64_t now = wtls_plat_now_ms();
        if (is_want(k)) {
            /* the io timeout of a non-blocking response is the longest wait without a single byte */
            if (r->io_timeout_ms && now - r->last_io_ms >= r->io_timeout_ms) return fail(r, WTLS_ERR_TIMEOUT);
            return k;
        }
        if (k > 0) r->last_io_ms = now;
    }
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
    wtls_free(r->conn);
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
    char *buf = wtls_malloc(cap + 1);
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
            char *nb = wtls_realloc(buf, ncap + 1);
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
        wtls_free(buf);
        return st;
    }
    buf[fill] = '\0';
    out->body = buf;
    out->body_len = fill;
    return WTLS_OK;
}

void wtls_http_result_free(wtls_http_result *res) {
    if (!res) return;
    wtls_free(res->body);
    res->body = NULL;
    res->body_len = 0;
}

#include "http/wtls_http_async.inc"
