/* HTTP/2 client session with stream multiplexing, driven without waiting (RFC 9113).
 * Design: docs/http2-multiplexing-design.md. */
#include "wtls_mem.h"
#include "http/wtls_h2s.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define F_HDR 9u
#define F_MAX 16384u            /* largest frame payload we accept or send */
#define DEF_WINDOW 65535u
#define OUT_SOFT (64u * 1024u)  /* DATA is accepted while less than this waits for the socket */
#define OUT_HARD (1024u * 1024u) /* control frames beyond this: the peer is flooding us */
#define CREDIT_STEP 16384u      /* connection-level WINDOW_UPDATE granularity */
#define FRAMES_PER_PUMP 64u

enum {
    T_DATA = 0, T_HEADERS = 1, T_PRIORITY = 2, T_RST = 3, T_SETTINGS = 4, T_PUSH = 5,
    T_PING = 6, T_GOAWAY = 7, T_WINDOW = 8, T_CONT = 9
};
enum { FL_END_STREAM = 1, FL_ACK = 1, FL_END_HEADERS = 4, FL_PADDED = 8, FL_PRIORITY = 0x20 };
enum { E_NO_ERROR = 0, E_PROTOCOL = 1, E_REFUSED = 7, E_CANCEL = 8 };

typedef struct out_chunk {
    struct out_chunk *next;
    size_t len, off;
    bool tried; /* offered to io.write: a WANT_WRITE retry must repeat it unchanged */
    uint8_t data[];
} out_chunk;

struct h2s_stream {
    h2s_session *s;
    uint32_t id; /* 0 until the headers are queued */
    int64_t send_win;
    int64_t recv_win;    /* what the peer may still send */
    uint32_t pending;    /* bytes the application consumed (or padding) that no WINDOW_UPDATE announced yet */
    uint8_t *buf;
    size_t off, used;
    wtls_http_head head;
    char *arena;
    bool have_head, head_taken;
    bool local_end, remote_end, rst;
    bool got_data, retryable;
    int err;
};

struct h2s_session {
    h2s_io io;
    void *io_ctx;
    wtls_hpack_ctx hpack;
    uint32_t max_streams, window;
    h2s_stream **tab;
    uint32_t next_id;
    uint32_t peer_initial_win, peer_max_frame, peer_max_conc;
    int64_t conn_send_win, conn_recv_win;
    uint32_t conn_credit;
    out_chunk *oh, *ot;
    size_t out_bytes;
    uint8_t *rx;
    size_t rx_len;
    /* header block assembly */
    bool in_cont, cont_end_stream;
    uint32_t cont_id;
    uint8_t *hblock;
    size_t hlen, hcap;
    bool goaway, unusable, more;
    bool got_settings; /* the peer's first SETTINGS arrived: until then its stream limit is unknown */
    uint32_t goaway_last;
    int err;
};

static uint32_t rd24(const uint8_t *p) { return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]; }
static uint32_t rd32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}
static void put_hdr(uint8_t *d, uint32_t len, uint8_t type, uint8_t flags, uint32_t id) {
    d[0] = (uint8_t)(len >> 16);
    d[1] = (uint8_t)(len >> 8);
    d[2] = (uint8_t)len;
    d[3] = type;
    d[4] = flags;
    wr32(d + 5, id & 0x7fffffffu);
}

/* ---- output queue ---- */

static int enqueue(h2s_session *s, out_chunk *c) {
    c->next = NULL;
    if (s->ot) s->ot->next = c;
    else s->oh = c;
    s->ot = c;
    s->out_bytes += c->len;
    return WTLS_OK;
}

static out_chunk *new_chunk(h2s_session *s, size_t len) {
    if (s->out_bytes + len > OUT_HARD) return NULL;
    out_chunk *c = wtls_malloc(sizeof *c + len);
    if (c) {
        c->len = len;
        c->off = 0;
        c->tried = false;
    }
    return c;
}

static int queue_frame(h2s_session *s, uint8_t type, uint8_t flags, uint32_t id, const void *pay, size_t len) {
    out_chunk *c = new_chunk(s, F_HDR + len);
    if (!c) return s->out_bytes + F_HDR + len > OUT_HARD ? WTLS_ERR_HTTP_LIMIT : WTLS_ERR_NOMEM;
    put_hdr(c->data, (uint32_t)len, type, flags, id);
    if (len) memcpy(c->data + F_HDR, pay, len);
    return enqueue(s, c);
}

static int queue_window_update(h2s_session *s, uint32_t id, uint32_t inc) {
    uint8_t p[4];
    wr32(p, inc);
    return queue_frame(s, T_WINDOW, 0, id, p, 4);
}

static void out_clear(h2s_session *s) {
    while (s->oh) {
        out_chunk *n = s->oh->next;
        wtls_free(s->oh);
        s->oh = n;
    }
    s->ot = NULL;
    s->out_bytes = 0;
}

/* ---- streams ---- */

static bool st_closed(const h2s_stream *st) { return st->rst || (st->remote_end && st->local_end); }

static h2s_stream *find(h2s_session *s, uint32_t id) {
    if (!id) return NULL;
    for (uint32_t i = 0; i < s->max_streams; i++)
        if (s->tab[i] && s->tab[i]->id == id) return s->tab[i];
    return NULL;
}

/* The server's concurrent stream limit as far as we know it: one stream until its SETTINGS are in (a server may
 * allow far fewer than the unlimited default, and a stream over its limit is refused). */
static uint32_t peer_limit(const h2s_session *s) { return s->got_settings ? s->peer_max_conc : 1u; }

static size_t started_open(const h2s_session *s) {
    size_t n = 0;
    for (uint32_t i = 0; i < s->max_streams; i++)
        if (s->tab[i] && s->tab[i]->id && !st_closed(s->tab[i])) n++;
    return n;
}

static void fail_session(h2s_session *s, int err) {
    if (!s->err) s->err = err ? err : WTLS_ERR_IO;
}

static bool conn_lost(int err) { return err == WTLS_ERR_IO || err == WTLS_ERR_CLOSED; }

/* A stream-level failure of our own making: reset it (best effort) and remember why. */
static void stream_fail(h2s_session *s, h2s_stream *st, int err, uint32_t code) {
    if (st->err || st->remote_end) return;
    st->err = err;
    if (!st->rst) {
        uint8_t p[4];
        wr32(p, code);
        if (queue_frame(s, T_RST, 0, st->id, p, 4) != WTLS_OK) fail_session(s, WTLS_ERR_NOMEM);
        st->rst = true;
    }
}

static void conn_credit_flush(h2s_session *s) {
    if (s->err || s->conn_credit < CREDIT_STEP) return;
    uint32_t inc = s->conn_credit;
    int st = queue_window_update(s, 0, inc);
    if (st != WTLS_OK) {
        fail_session(s, st);
        return;
    }
    s->conn_credit = 0;
    s->conn_recv_win += inc;
}

static void stream_credit(h2s_session *s, h2s_stream *st, uint32_t n) {
    s->conn_credit += n;
    st->pending += n;
    if (!st->remote_end && !st->rst && !s->err && st->pending >= s->window / 2u) {
        int r = queue_window_update(s, st->id, st->pending);
        if (r != WTLS_OK) fail_session(s, r);
        else {
            st->recv_win += st->pending;
            st->pending = 0;
        }
    }
    conn_credit_flush(s);
}

/* ---- frame handlers ---- */

static int on_settings(h2s_session *s, uint8_t flags, uint32_t id, const uint8_t *p, uint32_t len) {
    if (id != 0) return WTLS_ERR_HTTP;
    if (flags & FL_ACK) return len == 0 ? WTLS_OK : WTLS_ERR_HTTP;
    if (len % 6u) return WTLS_ERR_HTTP;
    s->got_settings = true;
    for (uint32_t o = 0; o < len; o += 6) {
        uint16_t sid = rd16(p + o);
        uint32_t v = rd32(p + o + 2);
        if (sid == 4) { /* INITIAL_WINDOW_SIZE */
            if (v > 0x7fffffffu) return WTLS_ERR_HTTP;
            int64_t delta = (int64_t)v - (int64_t)s->peer_initial_win;
            for (uint32_t i = 0; i < s->max_streams; i++) {
                h2s_stream *st = s->tab[i];
                if (!st || !st->id || st_closed(st)) continue;
                if (st->send_win + delta > 0x7fffffffLL) return WTLS_ERR_HTTP;
                st->send_win += delta;
            }
            s->peer_initial_win = v;
        } else if (sid == 5) { /* MAX_FRAME_SIZE */
            if (v < 16384u || v > 16777215u) return WTLS_ERR_HTTP;
            s->peer_max_frame = v > F_MAX ? F_MAX : v;
        } else if (sid == 3) { /* MAX_CONCURRENT_STREAMS */
            s->peer_max_conc = v;
        }
    }
    return queue_frame(s, T_SETTINGS, FL_ACK, 0, NULL, 0);
}

static int on_window_update(h2s_session *s, uint32_t id, const uint8_t *p, uint32_t len) {
    if (len != 4) return WTLS_ERR_HTTP;
    uint32_t inc = rd32(p) & 0x7fffffffu;
    if (inc == 0) return WTLS_ERR_HTTP;
    if (id == 0) {
        if (s->conn_send_win + (int64_t)inc > 0x7fffffffLL) return WTLS_ERR_HTTP;
        s->conn_send_win += inc;
        return WTLS_OK;
    }
    h2s_stream *st = find(s, id);
    if (st) {
        if (st->send_win + (int64_t)inc > 0x7fffffffLL) {
            stream_fail(s, st, WTLS_ERR_HTTP, E_PROTOCOL);
            return WTLS_OK;
        }
        st->send_win += inc;
        return WTLS_OK;
    }
    return id >= s->next_id ? WTLS_ERR_HTTP : WTLS_OK; /* idle stream: error; closed one: ignore */
}

static int on_goaway(h2s_session *s, uint32_t id, const uint8_t *p, uint32_t len) {
    if (id != 0 || len < 8) return WTLS_ERR_HTTP;
    uint32_t last = rd32(p) & 0x7fffffffu;
    if (!s->goaway || last < s->goaway_last) s->goaway_last = last;
    s->goaway = true;
    for (uint32_t i = 0; i < s->max_streams; i++) {
        h2s_stream *st = s->tab[i];
        if (!st || !st->id || st->id <= s->goaway_last || st->remote_end || st->err) continue;
        st->err = WTLS_ERR_IO;
        st->retryable = true; /* the server did not process it */
        st->rst = true;       /* nothing more to say on it */
    }
    return WTLS_OK;
}

static int on_rst(h2s_session *s, uint32_t id, const uint8_t *p, uint32_t len) {
    if (id == 0 || len != 4) return WTLS_ERR_HTTP;
    h2s_stream *st = find(s, id);
    if (!st) return id >= s->next_id ? WTLS_ERR_HTTP : WTLS_OK;
    uint32_t code = rd32(p);
    st->got_data = true;
    if (!st->remote_end && !st->err) {
        st->err = WTLS_ERR_IO;
        if (code == E_REFUSED) st->retryable = true;
    }
    st->rst = true;
    return WTLS_OK;
}

static int hblock_append(h2s_session *s, const uint8_t *p, size_t n) {
    if (n > WTLS_HTTP_MAX_HEAD - s->hlen) return WTLS_ERR_HTTP_LIMIT;
    if (s->hlen + n > s->hcap) {
        size_t nc = s->hcap ? s->hcap * 2u : 1024u;
        while (nc < s->hlen + n) nc *= 2u;
        if (nc > WTLS_HTTP_MAX_HEAD) nc = WTLS_HTTP_MAX_HEAD;
        uint8_t *nb = wtls_realloc(s->hblock, nc);
        if (!nb) return WTLS_ERR_NOMEM;
        s->hblock = nb;
        s->hcap = nc;
    }
    if (n) memcpy(s->hblock + s->hlen, p, n);
    s->hlen += n;
    return WTLS_OK;
}

static int finish_block(h2s_session *s) {
    uint32_t id = s->cont_id;
    bool end = s->cont_end_stream;
    s->in_cont = false;
    h2s_stream *st = find(s, id);
    int r;
    if (id >= s->next_id) return WTLS_ERR_HTTP;
    if (!st || st->err || st->rst) {
        r = wtls_hpack_decode_discard(&s->hpack, s->hblock, s->hlen);
        s->hlen = 0;
        return r;
    }
    st->got_data = true;
    if (st->have_head) { /* trailers */
        r = wtls_hpack_decode_trailers(&s->hpack, s->hblock, s->hlen);
        s->hlen = 0;
        if (r != WTLS_OK) return r;
        if (!end) stream_fail(s, st, WTLS_ERR_HTTP, E_PROTOCOL);
        else st->remote_end = true;
        return WTLS_OK;
    }
    wtls_http_head head;
    char *arena = NULL;
    r = wtls_hpack_decode_head(&s->hpack, s->hblock, s->hlen, &head, &arena);
    s->hlen = 0;
    if (r != WTLS_OK) return r;
    if (head.status >= 100 && head.status < 200) {
        wtls_free(arena);
        if (end || head.status == 101) stream_fail(s, st, WTLS_ERR_HTTP, E_PROTOCOL);
        return WTLS_OK;
    }
    st->head = head;
    st->arena = arena;
    st->have_head = true;
    if (end) st->remote_end = true;
    return WTLS_OK;
}

static int on_headers(h2s_session *s, uint8_t flags, uint32_t id, const uint8_t *p, uint32_t len) {
    if (id == 0 || (id & 1u) == 0) return WTLS_ERR_HTTP;
    size_t off = 0, pad = 0;
    if (flags & FL_PADDED) {
        if (len < 1) return WTLS_ERR_HTTP;
        pad = p[off++];
    }
    if (flags & FL_PRIORITY) {
        if (len - off < 5) return WTLS_ERR_HTTP;
        off += 5;
    }
    if (len < off || len - off < pad) return WTLS_ERR_HTTP;
    s->hlen = 0;
    s->cont_id = id;
    s->cont_end_stream = (flags & FL_END_STREAM) != 0;
    int r = hblock_append(s, p + off, len - off - pad);
    if (r != WTLS_OK) return r;
    if (flags & FL_END_HEADERS) return finish_block(s);
    s->in_cont = true;
    return WTLS_OK;
}

static int on_data(h2s_session *s, uint8_t flags, uint32_t id, const uint8_t *p, uint32_t len) {
    if (id == 0) return WTLS_ERR_HTTP;
    if (id >= s->next_id) return WTLS_ERR_HTTP;
    if ((int64_t)len > s->conn_recv_win) return WTLS_ERR_HTTP; /* FLOW_CONTROL_ERROR */
    s->conn_recv_win -= len;
    size_t off = 0, pad = 0;
    if (flags & FL_PADDED) {
        if (len < 1) return WTLS_ERR_HTTP;
        pad = p[off++];
        if (pad >= len) return WTLS_ERR_HTTP;
    }
    size_t n = len - off - pad;
    h2s_stream *st = find(s, id);
    if (!st || st->err || st->rst || st->remote_end || !st->have_head) {
        if (st && !st->err && !st->rst && !st->remote_end && !st->have_head) stream_fail(s, st, WTLS_ERR_HTTP, E_PROTOCOL);
        s->conn_credit += len; /* nobody will read it */
        conn_credit_flush(s);
        return WTLS_OK;
    }
    st->got_data = true;
    if ((int64_t)len > st->recv_win) return WTLS_ERR_HTTP;
    st->recv_win -= len;
    if (n) {
        if (!st->buf) {
            st->buf = wtls_malloc(s->window);
            if (!st->buf) return WTLS_ERR_NOMEM;
        }
        if (st->off + st->used + n > s->window) {
            memmove(st->buf, st->buf + st->off, st->used);
            st->off = 0;
        }
        memcpy(st->buf + st->off + st->used, p + off, n);
        st->used += n;
    }
    if (flags & FL_END_STREAM) st->remote_end = true;
    if (len - n) stream_credit(s, st, (uint32_t)(len - n)); /* pad length byte and padding */
    return s->err ? s->err : WTLS_OK;
}

static int process(h2s_session *s, uint8_t type, uint8_t flags, uint32_t id, const uint8_t *p, uint32_t len) {
    if (s->in_cont) {
        if (type != T_CONT || id != s->cont_id) return WTLS_ERR_HTTP;
        int r = hblock_append(s, p, len);
        if (r != WTLS_OK) return r;
        return (flags & FL_END_HEADERS) ? finish_block(s) : WTLS_OK;
    }
    switch (type) {
    case T_DATA: return on_data(s, flags, id, p, len);
    case T_HEADERS: return on_headers(s, flags, id, p, len);
    case T_PRIORITY: return (len == 5 && id != 0) ? WTLS_OK : WTLS_ERR_HTTP;
    case T_RST: return on_rst(s, id, p, len);
    case T_SETTINGS: return on_settings(s, flags, id, p, len);
    case T_PUSH: return WTLS_ERR_HTTP; /* ENABLE_PUSH is 0 */
    case T_PING:
        if (id != 0 || len != 8) return WTLS_ERR_HTTP;
        return (flags & FL_ACK) ? WTLS_OK : queue_frame(s, T_PING, FL_ACK, 0, p, 8);
    case T_GOAWAY: return on_goaway(s, id, p, len);
    case T_WINDOW: return on_window_update(s, id, p, len);
    case T_CONT: return WTLS_ERR_HTTP; /* without a HEADERS before it */
    default: return WTLS_OK; /* unknown types are ignored (RFC 9113 4.1) */
    }
}

/* ---- I/O ---- */

/* Merges the untouched chunks at the head of the queue (up to one full frame) into
 * one, so frames queued together leave in one write. The Wii cannot disable Nagle
 * (IOS rejects TCP_NODELAY): small writes in a row wait for the server's delayed
 * ACK. Without memory the chunks simply go out one by one. */
static void coalesce_head(h2s_session *s) {
    out_chunk *h = s->oh;
    if (!h || h->tried || !h->next) return;
    size_t total = 0;
    out_chunk *end = h;
    while (end && total + end->len <= F_HDR + F_MAX) {
        total += end->len;
        end = end->next;
    }
    if (end == h->next) return; /* only the head fits */
    out_chunk *m = wtls_malloc(sizeof *m + total);
    if (!m) return;
    m->len = total;
    m->off = 0;
    m->tried = false;
    size_t o = 0;
    for (out_chunk *p = h; p != end;) {
        out_chunk *n = p->next;
        memcpy(m->data + o, p->data, p->len);
        o += p->len;
        wtls_free(p);
        p = n;
    }
    m->next = end;
    s->oh = m;
    if (!end) s->ot = m;
}

static void flush_out(h2s_session *s) {
    while (s->oh && !s->err) {
        coalesce_head(s);
        out_chunk *c = s->oh;
        c->tried = true;
        int r = s->io.write(s->io_ctx, c->data + c->off, c->len - c->off);
        if (r == WTLS_WANT_WRITE || r == WTLS_WANT_READ) return;
        if (r <= 0) {
            fail_session(s, (r == 0 || r == WTLS_ERR_CLOSED) ? WTLS_ERR_IO : r);
            return;
        }
        c->off += (size_t)r;
        s->out_bytes -= (size_t)r;
        if (c->off >= c->len) {
            s->oh = c->next;
            if (!s->oh) s->ot = NULL;
            wtls_free(c);
        }
    }
}

static void read_in(h2s_session *s) {
    unsigned frames = 0;
    s->more = false;
    while (!s->err) {
        size_t need = F_HDR;
        if (s->rx_len >= F_HDR) {
            uint32_t len = rd24(s->rx);
            if (len > F_MAX) {
                fail_session(s, WTLS_ERR_HTTP);
                return;
            }
            need = F_HDR + len;
        }
        if (s->rx_len < need) {
            int r = s->io.read(s->io_ctx, s->rx + s->rx_len, need - s->rx_len);
            if (r == WTLS_WANT_READ || r == WTLS_WANT_WRITE) return;
            if (r <= 0) {
                fail_session(s, (r == 0) ? WTLS_ERR_CLOSED : r);
                return;
            }
            s->rx_len += (size_t)r;
            continue;
        }
        if (s->rx_len < F_HDR + rd24(s->rx)) continue;
        uint32_t len = rd24(s->rx);
        int r = process(s, s->rx[3], s->rx[4], rd32(s->rx + 5) & 0x7fffffffu, s->rx + F_HDR, len);
        s->rx_len = 0;
        if (r != WTLS_OK) {
            fail_session(s, r);
            return;
        }
        if (++frames >= FRAMES_PER_PUMP) {
            s->more = true;
            return;
        }
    }
}

int h2s_pump(h2s_session *s) {
    if (!s) return WTLS_ERR_ARG;
    if (s->err) return s->err;
    flush_out(s);
    if (!s->err) read_in(s);
    if (!s->err) flush_out(s);
    return s->err ? s->err : WTLS_OK;
}

/* ---- life cycle ---- */

h2s_session *h2s_create(const h2s_io *io, void *io_ctx, uint32_t max_streams, uint32_t stream_window) {
    if (!io || !io->read || !io->write) return NULL;
    if (max_streams < 1u) max_streams = 1u;
    if (max_streams > 64u) max_streams = 64u;
    if (stream_window < F_MAX) stream_window = F_MAX;
    if (stream_window > 0x7fffffffu / 64u) stream_window = 0x7fffffffu / 64u;
    h2s_session *s = wtls_calloc(1, sizeof *s);
    if (!s) return NULL;
    s->tab = wtls_calloc(max_streams, sizeof *s->tab);
    s->rx = wtls_malloc(F_HDR + F_MAX);
    if (!s->tab || !s->rx) goto fail;
    s->io = *io;
    s->io_ctx = io_ctx;
    s->max_streams = max_streams;
    s->window = stream_window;
    s->next_id = 1u;
    s->peer_initial_win = DEF_WINDOW;
    s->peer_max_frame = F_MAX;
    s->peer_max_conc = 0xffffffffu;
    s->conn_send_win = DEF_WINDOW;
    s->conn_recv_win = DEF_WINDOW;
    wtls_hpack_init(&s->hpack, WTLS_HPACK_DEFAULT_TABLE);

    uint64_t total = (uint64_t)max_streams * stream_window;
    uint32_t target = total > DEF_WINDOW ? (uint32_t)(total > 0x7fffffffu ? 0x7fffffffu : total) : DEF_WINDOW;
    uint8_t init[24 + 9 + 18 + 9 + 4];
    size_t n = 0;
    memcpy(init, "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", 24);
    n = 24;
    put_hdr(init + n, 18, T_SETTINGS, 0, 0);
    n += F_HDR;
    wr16(init + n, 2);
    wr32(init + n + 2, 0); /* ENABLE_PUSH */
    wr16(init + n + 6, 4);
    wr32(init + n + 8, stream_window); /* INITIAL_WINDOW_SIZE */
    wr16(init + n + 12, 6);
    wr32(init + n + 14, WTLS_HTTP_MAX_HEAD); /* MAX_HEADER_LIST_SIZE */
    n += 18;
    if (target > DEF_WINDOW) {
        put_hdr(init + n, 4, T_WINDOW, 0, 0);
        wr32(init + n + F_HDR, target - DEF_WINDOW);
        n += F_HDR + 4;
        s->conn_recv_win = target;
    }
    out_chunk *c = new_chunk(s, n);
    if (!c) goto fail;
    memcpy(c->data, init, n);
    enqueue(s, c);
    return s;
fail:
    wtls_free(s->tab);
    wtls_free(s->rx);
    wtls_free(s);
    return NULL;
}

static void stream_free(h2s_stream *st) {
    if (!st) return;
    wtls_free(st->buf);
    wtls_free(st->arena);
    wtls_free(st);
}

void h2s_destroy(h2s_session *s) {
    if (!s) return;
    for (uint32_t i = 0; i < s->max_streams; i++) stream_free(s->tab[i]);
    out_clear(s);
    wtls_hpack_free(&s->hpack);
    wtls_free(s->hblock);
    wtls_free(s->tab);
    wtls_free(s->rx);
    wtls_free(s);
}

bool h2s_pending(const h2s_session *s) { return s && s->more && !s->err; }

unsigned h2s_wants(const h2s_session *s) { return 1u | (s && s->oh ? 2u : 0u); }

bool h2s_settings_known(const h2s_session *s) { return s && s->got_settings; }

bool h2s_usable(const h2s_session *s) { return s && !s->err && !s->goaway && !s->unusable; }

size_t h2s_active(const h2s_session *s) {
    size_t n = 0;
    for (uint32_t i = 0; s && i < s->max_streams; i++)
        if (s->tab[i]) n++;
    return n;
}

size_t h2s_capacity(const h2s_session *s) {
    if (!s) return 0;
    uint32_t lim = peer_limit(s);
    return lim < s->max_streams ? lim : s->max_streams;
}

int h2s_error(const h2s_session *s) { return s ? s->err : WTLS_ERR_ARG; }

/* ---- streams: application side ---- */

int h2s_stream_open(h2s_session *s, h2s_stream **out) {
    if (!s || !out) return WTLS_ERR_ARG;
    *out = NULL;
    if (!h2s_usable(s)) return WTLS_ERR_IO;
    uint32_t slot = s->max_streams;
    for (uint32_t i = 0; i < s->max_streams; i++)
        if (!s->tab[i]) {
            slot = i;
            break;
        }
    if (slot == s->max_streams) return WTLS_WANT_READ;
    if (started_open(s) >= peer_limit(s)) return WTLS_WANT_READ;
    h2s_stream *st = wtls_calloc(1, sizeof *st);
    if (!st) return WTLS_ERR_NOMEM;
    st->s = s;
    st->recv_win = s->window;
    s->tab[slot] = st;
    *out = st;
    return WTLS_OK;
}

int h2s_stream_send_headers(h2s_stream *st, const char *method, const wtls_url *u, const wtls_http_header *h, size_t n,
                            bool has_content_length, size_t body_len, bool has_body, bool want_gzip) {
    h2s_session *s = st->s;
    if (s->err) return s->err;
    if (st->id) return WTLS_ERR_STATE;
    if (!h2s_usable(s)) return WTLS_ERR_IO;
    if (s->next_id > 0x7fffffffu) {
        s->unusable = true;
        return WTLS_ERR_IO;
    }
    if (started_open(s) >= peer_limit(s)) return WTLS_WANT_READ;
    uint8_t *blk = NULL;
    size_t blk_len = 0;
    int r = wtls_hpack_encode_request(method, u, h, n, has_content_length, body_len, want_gzip, false, &blk, &blk_len);
    if (r != WTLS_OK) return r;
    uint32_t id = s->next_id;
    size_t off = 0;
    bool first = true;
    while (first || off < blk_len) {
        size_t chunk = blk_len - off;
        if (chunk > s->peer_max_frame) chunk = s->peer_max_frame;
        uint8_t flags = (off + chunk == blk_len ? FL_END_HEADERS : 0u) | (first && !has_body ? FL_END_STREAM : 0u);
        r = queue_frame(s, first ? T_HEADERS : T_CONT, flags, id, blk + off, chunk);
        if (r != WTLS_OK) break;
        off += chunk;
        first = false;
    }
    wtls_free(blk);
    if (r != WTLS_OK) {
        if (off) fail_session(s, r); /* half a header block is on the wire */
        return r;
    }
    s->next_id += 2u;
    st->id = id;
    st->send_win = s->peer_initial_win;
    st->local_end = !has_body;
    return WTLS_OK;
}

int h2s_stream_send_data(h2s_stream *st, const uint8_t *data, size_t len, bool end_stream) {
    h2s_session *s = st->s;
    if (!st->id || st->local_end) return WTLS_ERR_STATE;
    if (st->err) return st->err;
    if (s->err) return s->err;
    if (st->rst) return WTLS_ERR_IO;
    if (len == 0) {
        if (!end_stream) return 0;
        int r = queue_frame(s, T_DATA, FL_END_STREAM, st->id, NULL, 0);
        if (r != WTLS_OK) return r;
        st->local_end = true;
        return 0;
    }
    if (s->out_bytes >= OUT_SOFT) return 0;
    int64_t room = st->send_win < s->conn_send_win ? st->send_win : s->conn_send_win;
    if (room <= 0) return 0;
    size_t n = len;
    if (n > s->peer_max_frame) n = s->peer_max_frame;
    if ((int64_t)n > room) n = (size_t)room;
    bool last = end_stream && n == len;
    out_chunk *c = new_chunk(s, F_HDR + n);
    if (!c) return WTLS_ERR_NOMEM;
    put_hdr(c->data, (uint32_t)n, T_DATA, last ? FL_END_STREAM : 0u, st->id);
    memcpy(c->data + F_HDR, data, n);
    enqueue(s, c);
    st->send_win -= (int64_t)n;
    s->conn_send_win -= (int64_t)n;
    if (last) st->local_end = true;
    return (int)n;
}

static int stream_err(const h2s_stream *st) {
    if (st->err) return st->err;
    if (st->s->err && !st->remote_end) return st->s->err;
    return 0;
}

int h2s_stream_error(const h2s_stream *st) { return st ? stream_err(st) : WTLS_ERR_ARG; }

bool h2s_stream_retryable(const h2s_stream *st) {
    if (!st || !stream_err(st)) return false;
    if (st->retryable) return true;
    return !st->err && !st->got_data && conn_lost(st->s->err);
}

bool h2s_stream_got_data(const h2s_stream *st) { return st && st->got_data; }
bool h2s_stream_request_done(const h2s_stream *st) { return st && st->id && st->local_end; }
bool h2s_stream_ready(const h2s_stream *st) {
    if (!st) return false;
    return (st->have_head && !st->head_taken) || st->used || st->remote_end || stream_err(st);
}
bool h2s_stream_ended(const h2s_stream *st) { return st && st->remote_end; }
size_t h2s_stream_buffered(const h2s_stream *st) { return st ? st->used : 0; }

int h2s_stream_head(h2s_stream *st, wtls_http_head *head, char **arena) {
    if (st->have_head) {
        if (st->head_taken) return WTLS_ERR_STATE;
        *head = st->head;
        *arena = st->arena;
        st->arena = NULL;
        st->head_taken = true;
        return WTLS_OK;
    }
    int e = stream_err(st);
    return e ? e : WTLS_WANT_READ;
}

int h2s_stream_read(h2s_stream *st, void *buf, size_t len, bool *eof) {
    *eof = false;
    if (!st->head_taken) return WTLS_ERR_STATE;
    if (st->used) {
        size_t n = len < st->used ? len : st->used;
        memcpy(buf, st->buf + st->off, n);
        st->off += n;
        st->used -= n;
        if (!st->used) st->off = 0;
        stream_credit(st->s, st, (uint32_t)n);
        return (int)n;
    }
    if (st->remote_end) {
        *eof = true;
        return 0;
    }
    int e = stream_err(st);
    return e ? e : WTLS_WANT_READ;
}

void h2s_stream_release(h2s_stream *st) {
    if (!st) return;
    h2s_session *s = st->s;
    if (st->id && !st_closed(st) && !s->err) {
        uint8_t p[4];
        wr32(p, E_CANCEL);
        if (queue_frame(s, T_RST, 0, st->id, p, 4) != WTLS_OK) fail_session(s, WTLS_ERR_NOMEM);
    }
    s->conn_credit += (uint32_t)st->used;
    for (uint32_t i = 0; i < s->max_streams; i++)
        if (s->tab[i] == st) s->tab[i] = NULL;
    stream_free(st);
    conn_credit_flush(s);
}
