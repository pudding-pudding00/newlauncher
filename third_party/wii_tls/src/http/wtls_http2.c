/* HTTP/2 client connection and sequential stream engine (RFC 9113). */
#include "http/wtls_http2.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "plat/plat.h"
#include "wtls_internal.h"

#define H2_FRAME_HDR 9u
#define H2_DEF_WINDOW 65535u
#define H2_LOCAL_WINDOW 262144u
#define H2_LOCAL_MAX_FRAME 16384u
#define H2_MAX_FRAME_CAP 16384u

/* Frame types (RFC 9113 §6) */
enum {
    H2_DATA = 0x0,
    H2_HEADERS = 0x1,
    H2_PRIORITY = 0x2,
    H2_RST_STREAM = 0x3,
    H2_SETTINGS = 0x4,
    H2_PUSH_PROMISE = 0x5,
    H2_PING = 0x6,
    H2_GOAWAY = 0x7,
    H2_WINDOW_UPDATE = 0x8,
    H2_CONTINUATION = 0x9
};

/* Frame flags */
enum {
    H2_FLAG_ACK = 0x01,
    H2_FLAG_END_STREAM = 0x01,
    H2_FLAG_END_HEADERS = 0x04,
    H2_FLAG_PADDED = 0x08,
    H2_FLAG_PRIORITY = 0x20
};

/* Error codes */
enum {
    H2_NO_ERROR = 0x0,
    H2_CANCEL = 0x8
};

struct wtls_h2_conn {
    wtls_conn *tls;
    wtls_hpack_ctx hpack;
    uint32_t next_stream_id;
    uint32_t cur_stream_id;
    uint32_t peer_initial_win;
    uint32_t peer_max_frame;
    int64_t conn_send_win;
    int64_t conn_recv_win;
    int64_t stream_send_win;
    int64_t stream_recv_win;
    bool stream_remote_eof;
    bool stream_rst;
    bool goaway_recv;
    bool broken;
    uint8_t *rx_buf;
    size_t rx_off;
    size_t rx_end;
};

static uint32_t read_u24(const uint8_t *p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
}

static uint16_t read_u16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }

static uint32_t read_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void write_u24(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 16);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)v;
}

static void write_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void write_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void write_frame_hdr(uint8_t *dst, uint32_t len, uint8_t type, uint8_t flags, uint32_t stream) {
    write_u24(dst, len);
    dst[3] = type;
    dst[4] = flags;
    write_u32(dst + 5, stream & 0x7fffffffu);
}

static int h2_write_all(wtls_h2_conn *c, const void *buf, size_t len) {
    const uint8_t *p = buf;
    while (len) {
        int k = wtls_write(c->tls, p, len);
        if (k <= 0) {
            c->broken = true;
            return k == WTLS_ERR_CLOSED || k == 0 ? WTLS_ERR_IO : k;
        }
        p += k;
        len -= (size_t)k;
    }
    return WTLS_OK;
}

static int h2_read_exact(wtls_h2_conn *c, void *buf, size_t len, uint64_t has_deadline, uint64_t deadline) {
    uint8_t *p = buf;
    uint32_t saved_to = c->tls->io_timeout_ms;
    while (len) {
        if (has_deadline) {
            uint64_t now = wtls_plat_now_ms();
            if (now >= deadline) {
                c->tls->io_timeout_ms = saved_to;
                return WTLS_ERR_TIMEOUT;
            }
            uint64_t left = deadline - now;
            if (left < c->tls->io_timeout_ms) c->tls->io_timeout_ms = (uint32_t)left;
        }
        int k = wtls_read(c->tls, p, len);
        c->tls->io_timeout_ms = saved_to;
        if (k <= 0) {
            if (k == WTLS_ERR_CLOSED) k = WTLS_ERR_IO;
            c->broken = true;
            return k;
        }
        p += k;
        len -= (size_t)k;
    }
    return WTLS_OK;
}

static int send_frame(wtls_h2_conn *c, uint8_t type, uint8_t flags, uint32_t stream,
                      const void *payload, size_t len) {
    uint8_t hdr[H2_FRAME_HDR];
    write_frame_hdr(hdr, (uint32_t)len, type, flags, stream);
    int st = h2_write_all(c, hdr, sizeof hdr);
    if (st == WTLS_OK && len) st = h2_write_all(c, payload, len);
    return st;
}

static int send_window_update(wtls_h2_conn *c, uint32_t stream, uint32_t inc) {
    uint8_t pay[4];
    write_u32(pay, inc & 0x7fffffffu);
    return send_frame(c, H2_WINDOW_UPDATE, 0, stream, pay, sizeof pay);
}

typedef struct {
    uint32_t len;
    uint8_t type;
    uint8_t flags;
    uint32_t stream;
    uint8_t *payload;
} h2_frame;

static int read_frame(wtls_h2_conn *c, h2_frame *f, bool use_deadline, uint64_t deadline) {
    uint8_t hdr[H2_FRAME_HDR];
    int st = h2_read_exact(c, hdr, sizeof hdr, use_deadline, deadline);
    if (st != WTLS_OK) return st;
    f->len = read_u24(hdr);
    f->type = hdr[3];
    f->flags = hdr[4];
    f->stream = read_u32(hdr + 5) & 0x7fffffffu;
    f->payload = c->rx_buf;
    if (f->len > H2_MAX_FRAME_CAP) {
        c->broken = true;
        return WTLS_ERR_HTTP;
    }
    if (f->len > 0) {
        st = h2_read_exact(c, c->rx_buf, f->len, use_deadline, deadline);
        if (st != WTLS_OK) return st;
    }
    return WTLS_OK;
}

static int apply_settings(wtls_h2_conn *c, const h2_frame *f) {
    if (f->stream != 0) return WTLS_ERR_HTTP;
    if (f->flags & H2_FLAG_ACK) return f->len == 0 ? WTLS_OK : WTLS_ERR_HTTP;
    if (f->len % 6 != 0) return WTLS_ERR_HTTP;
    for (uint32_t off = 0; off < f->len; off += 6) {
        uint16_t id = read_u16(f->payload + off);
        uint32_t val = read_u32(f->payload + off + 2);
        if (id == 0x04) { /* SETTINGS_INITIAL_WINDOW_SIZE */
            if (val > 0x7fffffffu) return WTLS_ERR_HTTP;
            int64_t delta = (int64_t)val - (int64_t)c->peer_initial_win;
            if (c->cur_stream_id) {
                int64_t nw = c->stream_send_win + delta;
                if (nw > 0x7fffffffLL) return WTLS_ERR_HTTP;
                c->stream_send_win = nw;
            }
            c->peer_initial_win = val;
        } else if (id == 0x05) { /* SETTINGS_MAX_FRAME_SIZE */
            if (val < 16384u || val > 16777215u) return WTLS_ERR_HTTP;
            c->peer_max_frame = val > H2_MAX_FRAME_CAP ? H2_MAX_FRAME_CAP : val;
        }
    }
    return send_frame(c, H2_SETTINGS, H2_FLAG_ACK, 0, NULL, 0);
}

static int apply_window_update(wtls_h2_conn *c, const h2_frame *f) {
    if (f->len != 4) return WTLS_ERR_HTTP;
    uint32_t inc = read_u32(f->payload) & 0x7fffffffu;
    if (inc == 0) return WTLS_ERR_HTTP;
    if (f->stream == 0) {
        if (c->conn_send_win + (int64_t)inc > 0x7fffffffLL) return WTLS_ERR_HTTP;
        c->conn_send_win += (int64_t)inc;
    } else if (f->stream == c->cur_stream_id) {
        if (c->stream_send_win + (int64_t)inc > 0x7fffffffLL) return WTLS_ERR_HTTP;
        c->stream_send_win += (int64_t)inc;
    }
    return WTLS_OK;
}

static int handle_common_frame(wtls_h2_conn *c, const h2_frame *f) {
    switch (f->type) {
    case H2_SETTINGS:
        return apply_settings(c, f);
    case H2_PING:
        if (f->stream != 0 || f->len != 8) return WTLS_ERR_HTTP;
        if (!(f->flags & H2_FLAG_ACK)) return send_frame(c, H2_PING, H2_FLAG_ACK, 0, f->payload, 8);
        return WTLS_OK;
    case H2_WINDOW_UPDATE:
        return apply_window_update(c, f);
    case H2_GOAWAY:
        if (f->stream != 0 || f->len < 8) return WTLS_ERR_HTTP;
        c->goaway_recv = true;
        if (c->cur_stream_id && c->cur_stream_id > (read_u32(f->payload) & 0x7fffffffu)) {
            c->stream_rst = true;
            c->stream_remote_eof = true;
            return WTLS_ERR_IO;
        }
        return WTLS_OK;
    case H2_RST_STREAM:
        if (f->stream == 0 || f->len != 4) return WTLS_ERR_HTTP;
        if (f->stream == c->cur_stream_id) {
            uint32_t err_code = read_u32(f->payload);
            c->stream_rst = true;
            c->stream_remote_eof = true;
            if (err_code == H2_NO_ERROR) return WTLS_OK;
            return WTLS_ERR_IO;
        }
        return WTLS_OK;
    case H2_PUSH_PROMISE:
        return WTLS_ERR_HTTP; /* ENABLE_PUSH is 0 */
    case H2_PRIORITY:
        return f->len == 5 && f->stream != 0 ? WTLS_OK : WTLS_ERR_HTTP;
    default:
        return WTLS_OK; /* ignore unknown frame types per RFC 9113 §4.1 */
    }
}

static int decode_headers_sequence(wtls_h2_conn *c, const h2_frame *first, bool use_deadline,
                                   uint64_t deadline, wtls_http_head *head_out, char **arena_out) {
    if (first->stream != c->cur_stream_id || first->stream == 0) return WTLS_ERR_HTTP;
    size_t off = 0;
    size_t pad = 0;
    if (first->flags & H2_FLAG_PADDED) {
        if (first->len < 1) return WTLS_ERR_HTTP;
        pad = first->payload[off++];
    }
    if (first->flags & H2_FLAG_PRIORITY) {
        if (first->len - off < 5) return WTLS_ERR_HTTP;
        off += 5;
    }
    if (first->len < off || first->len - off < pad) return WTLS_ERR_HTTP;
    size_t frag_len = first->len - off - pad;
    if (frag_len > WTLS_HTTP_MAX_HEAD) return WTLS_ERR_HTTP_LIMIT;

    uint8_t *blk = malloc(frag_len ? frag_len : 1);
    if (!blk) return WTLS_ERR_NOMEM;
    if (frag_len) memcpy(blk, first->payload + off, frag_len);
    size_t blk_len = frag_len;

    bool end_stream = (first->flags & H2_FLAG_END_STREAM) != 0;
    bool end_headers = (first->flags & H2_FLAG_END_HEADERS) != 0;
    int st = WTLS_OK;
    while (!end_headers) {
        h2_frame cont;
        st = read_frame(c, &cont, use_deadline, deadline);
        if (st != WTLS_OK) break;
        if (cont.type != H2_CONTINUATION || cont.stream != c->cur_stream_id) {
            st = WTLS_ERR_HTTP;
            break;
        }
        if (cont.len > WTLS_HTTP_MAX_HEAD - blk_len) {
            st = WTLS_ERR_HTTP_LIMIT;
            break;
        }
        if (cont.len) {
            uint8_t *nb = realloc(blk, blk_len + cont.len);
            if (!nb) {
                st = WTLS_ERR_NOMEM;
                break;
            }
            blk = nb;
            memcpy(blk + blk_len, cont.payload, cont.len);
            blk_len += cont.len;
        }
        if (cont.flags & H2_FLAG_END_HEADERS) end_headers = true;
    }
    if (st == WTLS_OK) st = wtls_hpack_decode_head(&c->hpack, blk, blk_len, head_out, arena_out);
    free(blk);
    if (st != WTLS_OK) return st;
    if (head_out->status >= 100 && head_out->status < 200) {
        if (end_stream) {
            free(*arena_out);
            *arena_out = NULL;
            return WTLS_ERR_HTTP;
        }
    } else if (end_stream) {
        c->stream_remote_eof = true;
    }
    return WTLS_OK;
}

static int replenish_recv_windows(wtls_h2_conn *c, uint32_t frame_len) {
    if (!frame_len) return WTLS_OK;
    c->conn_recv_win -= (int64_t)frame_len;
    c->stream_recv_win -= (int64_t)frame_len;
    if (c->conn_recv_win < 0 || c->stream_recv_win < 0) return WTLS_ERR_HTTP;
    if (c->conn_recv_win <= (int64_t)(H2_LOCAL_WINDOW / 2u)) {
        uint32_t inc = (uint32_t)(H2_LOCAL_WINDOW - c->conn_recv_win);
        c->conn_recv_win += inc;
        int st = send_window_update(c, 0, inc);
        if (st != WTLS_OK) return st;
    }
    if (!c->stream_remote_eof && c->stream_recv_win <= (int64_t)(H2_LOCAL_WINDOW / 2u)) {
        uint32_t inc = (uint32_t)(H2_LOCAL_WINDOW - c->stream_recv_win);
        c->stream_recv_win += inc;
        int st = send_window_update(c, c->cur_stream_id, inc);
        if (st != WTLS_OK) return st;
    }
    return WTLS_OK;
}

static int extract_data_frame(wtls_h2_conn *c, const h2_frame *f, size_t *data_off, size_t *data_len) {
    if (f->stream != c->cur_stream_id || f->stream == 0) return WTLS_ERR_HTTP;
    size_t off = 0, pad = 0;
    if (f->flags & H2_FLAG_PADDED) {
        if (f->len < 1) return WTLS_ERR_HTTP;
        pad = f->payload[off++];
    }
    if (f->len - off < pad) return WTLS_ERR_HTTP;
    *data_off = off;
    *data_len = f->len - off - pad;
    if (f->flags & H2_FLAG_END_STREAM) c->stream_remote_eof = true;
    return replenish_recv_windows(c, f->len);
}

/* ---- Public internal API -------------------------------------------------- */

int wtls_h2_create(wtls_conn *tls, wtls_h2_conn **out) {
    if (!tls || !out) return WTLS_ERR_ARG;
    *out = NULL;
    wtls_h2_conn *c = calloc(1, sizeof *c);
    if (!c) return WTLS_ERR_NOMEM;
    c->rx_buf = malloc(H2_MAX_FRAME_CAP);
    if (!c->rx_buf) {
        free(c);
        return WTLS_ERR_NOMEM;
    }
    c->tls = tls;
    wtls_hpack_init(&c->hpack, WTLS_HPACK_DEFAULT_TABLE);
    c->next_stream_id = 1u;
    c->peer_initial_win = H2_DEF_WINDOW;
    c->peer_max_frame = H2_LOCAL_MAX_FRAME;
    c->conn_send_win = H2_DEF_WINDOW;
    c->conn_recv_win = H2_DEF_WINDOW;

    /* Client preface (24) + SETTINGS (9 + 18) + conn WINDOW_UPDATE (9 + 4) = 64 bytes */
    uint8_t init_msg[24 + 9 + 18 + 9 + 4];
    memcpy(init_msg, "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", 24);
    write_frame_hdr(init_msg + 24, 18, H2_SETTINGS, 0, 0);
    write_u16(init_msg + 33, 0x02); /* SETTINGS_ENABLE_PUSH */
    write_u32(init_msg + 35, 0);
    write_u16(init_msg + 39, 0x04); /* SETTINGS_INITIAL_WINDOW_SIZE */
    write_u32(init_msg + 41, H2_LOCAL_WINDOW);
    write_u16(init_msg + 45, 0x06); /* SETTINGS_MAX_HEADER_LIST_SIZE */
    write_u32(init_msg + 47, WTLS_HTTP_MAX_HEAD);

    uint32_t win_inc = H2_LOCAL_WINDOW - H2_DEF_WINDOW;
    write_frame_hdr(init_msg + 51, 4, H2_WINDOW_UPDATE, 0, 0);
    write_u32(init_msg + 60, win_inc);
    c->conn_recv_win += win_inc;

    int st = h2_write_all(c, init_msg, sizeof init_msg);
    if (st != WTLS_OK) {
        wtls_h2_free(c);
        return st;
    }
    *out = c;
    return WTLS_OK;
}

void wtls_h2_free(wtls_h2_conn *c) {
    if (!c) return;
    wtls_hpack_free(&c->hpack);
    free(c->rx_buf);
    free(c);
}

bool wtls_h2_reusable(const wtls_h2_conn *c) {
    return c && !c->goaway_recv && !c->broken && c->next_stream_id <= 0x7fffffffu;
}

bool wtls_h2_alive(wtls_h2_conn *c) {
    if (!wtls_h2_reusable(c)) return false;
    c->cur_stream_id = 0;
    for (;;) {
        if (!wtls_pending(c->tls) && wtls_plat_poll(c->tls->fd, true, false, 0) == 0)
            return wtls_h2_reusable(c);
        uint8_t hdr[H2_FRAME_HDR];
        if (wtls_set_nonblocking(c->tls, true) != WTLS_OK) return false;
        int k = wtls_read(c->tls, hdr, sizeof hdr);
        wtls_set_nonblocking(c->tls, false);
        if (k == WTLS_WANT_READ) return wtls_h2_reusable(c);
        if (k <= 0) return false;
        size_t got = (size_t)k;
        uint64_t dl = wtls_plat_now_ms() + 200u;
        if (got < sizeof hdr) {
            if (h2_read_exact(c, hdr + got, sizeof hdr - got, true, dl) != WTLS_OK) return false;
        }
        h2_frame f;
        f.len = read_u24(hdr);
        f.type = hdr[3];
        f.flags = hdr[4];
        f.stream = read_u32(hdr + 5) & 0x7fffffffu;
        f.payload = c->rx_buf;
        if (f.len > H2_MAX_FRAME_CAP) return false;
        if (f.len > 0 && h2_read_exact(c, c->rx_buf, f.len, true, dl) != WTLS_OK) return false;
        if (f.type != H2_SETTINGS && f.type != H2_PING && f.type != H2_WINDOW_UPDATE &&
            f.type != H2_GOAWAY)
            return false;
        if (handle_common_frame(c, &f) != WTLS_OK) return false;
        if (c->goaway_recv) return false;
    }
}

int wtls_h2_begin_stream(wtls_h2_conn *c) {
    if (!c || c->broken || c->goaway_recv || c->next_stream_id > 0x7fffffffu) return WTLS_ERR_IO;
    c->cur_stream_id = c->next_stream_id;
    c->next_stream_id += 2u;
    c->stream_send_win = (int64_t)c->peer_initial_win;
    c->stream_recv_win = H2_LOCAL_WINDOW;
    c->stream_remote_eof = false;
    c->stream_rst = false;
    c->rx_off = c->rx_end = 0;
    return WTLS_OK;
}

int wtls_h2_send_headers(wtls_h2_conn *c, const char *method, const wtls_url *u,
                         const wtls_http_header *h, size_t n_headers,
                         bool has_content_length, size_t body_len, bool has_body,
                         bool want_gzip, bool expect_100) {
    uint8_t *blk = NULL;
    size_t blk_len = 0;
    int st = wtls_hpack_encode_request(method, u, h, n_headers, has_content_length, body_len,
                                       want_gzip, expect_100, &blk, &blk_len);
    if (st != WTLS_OK) return st;

    size_t off = 0;
    bool first = true;
    while (first || off < blk_len) {
        size_t chunk = blk_len - off;
        if (chunk > c->peer_max_frame) chunk = c->peer_max_frame;
        bool last = (off + chunk == blk_len);
        uint8_t flags = (last ? H2_FLAG_END_HEADERS : 0u) |
                        (first && !has_body ? H2_FLAG_END_STREAM : 0u);
        st = send_frame(c, first ? H2_HEADERS : H2_CONTINUATION, flags, c->cur_stream_id,
                        blk + off, chunk);
        if (st != WTLS_OK) break;
        off += chunk;
        first = false;
    }
    free(blk);
    return st;
}

int wtls_h2_send_data(wtls_h2_conn *c, const uint8_t *data, size_t len, bool end_stream,
                      bool *early_final, wtls_http_head *head_out, char **arena_out, bool *got_head) {
    if (early_final) *early_final = false;
    size_t off = 0;
    do {
        size_t left = len - off;
        if (left > 0) {
            while (c->conn_send_win <= 0 || c->stream_send_win <= 0) {
                h2_frame f;
                int st = read_frame(c, &f, false, 0);
                if (st != WTLS_OK) return st;
                if (f.type == H2_HEADERS && f.stream == c->cur_stream_id && head_out && arena_out) {
                    wtls_http_head tmp;
                    char *arena = NULL;
                    st = decode_headers_sequence(c, &f, false, 0, &tmp, &arena);
                    if (st != WTLS_OK) {
                        c->broken = true;
                        return st;
                    }
                    if (tmp.status >= 200) {
                        *head_out = tmp;
                        *arena_out = arena;
                        if (got_head) *got_head = true;
                        if (early_final) *early_final = true;
                        return WTLS_OK;
                    }
                    free(arena);
                    continue;
                }
                st = handle_common_frame(c, &f);
                if (st != WTLS_OK) return st;
            }
        }
        size_t chunk = left;
        if ((int64_t)chunk > c->conn_send_win) chunk = (size_t)c->conn_send_win;
        if ((int64_t)chunk > c->stream_send_win) chunk = (size_t)c->stream_send_win;
        if (chunk > c->peer_max_frame) chunk = c->peer_max_frame;
        bool last = (off + chunk == len) && end_stream;
        int st = send_frame(c, H2_DATA, last ? H2_FLAG_END_STREAM : 0u, c->cur_stream_id,
                            data + off, chunk);
        if (st != WTLS_OK) return st;
        c->conn_send_win -= (int64_t)chunk;
        c->stream_send_win -= (int64_t)chunk;
        off += chunk;
    } while (off < len);
    return WTLS_OK;
}

int wtls_h2_wait_continue(wtls_h2_conn *c, uint32_t continue_timeout_ms, uint32_t header_timeout_ms,
                          bool *early_final, wtls_http_head *head_out, char **arena_out, bool *got_head) {
    *early_final = false;
    if (got_head) *got_head = false;
    uint64_t dl_100 = wtls_plat_now_ms() + continue_timeout_ms;
    uint64_t dl_hdr = wtls_plat_now_ms() + header_timeout_ms;
    for (;;) {
        h2_frame f;
        int st = read_frame(c, &f, true, dl_100);
        if (st == WTLS_ERR_TIMEOUT) return WTLS_OK; /* continue without 100 response */
        if (st != WTLS_OK) return st;
        if (f.type == H2_HEADERS && f.stream == c->cur_stream_id) {
            wtls_http_head tmp;
            char *arena = NULL;
            st = decode_headers_sequence(c, &f, true, dl_hdr, &tmp, &arena);
            if (st != WTLS_OK) {
                c->broken = true;
                return st;
            }
            if (tmp.status == 100) {
                free(arena);
                return WTLS_OK;
            }
            if (tmp.status >= 200) {
                *head_out = tmp;
                *arena_out = arena;
                if (got_head) *got_head = true;
                *early_final = true;
                return WTLS_OK;
            }
            free(arena);
            continue;
        }
        st = handle_common_frame(c, &f);
        if (st != WTLS_OK) return st;
    }
}

int wtls_h2_read_head(wtls_h2_conn *c, uint32_t header_timeout_ms, wtls_http_head *head_out,
                      char **arena_out, bool *got_head) {
    if (got_head) *got_head = false;
    uint64_t dl = wtls_plat_now_ms() + header_timeout_ms;
    for (;;) {
        h2_frame f;
        int st = read_frame(c, &f, true, dl);
        if (st != WTLS_OK) return st;
        if (f.type == H2_HEADERS && f.stream == c->cur_stream_id) {
            wtls_http_head tmp;
            char *arena = NULL;
            st = decode_headers_sequence(c, &f, true, dl, &tmp, &arena);
            if (st != WTLS_OK) {
                c->broken = true;
                return st;
            }
            if (got_head) *got_head = true;
            if (tmp.status < 200) {
                free(arena);
                dl = wtls_plat_now_ms() + header_timeout_ms;
                continue;
            }
            *head_out = tmp;
            *arena_out = arena;
            return WTLS_OK;
        }
        st = handle_common_frame(c, &f);
        if (st != WTLS_OK) return st;
    }
}

int wtls_h2_read_data(wtls_h2_conn *c, void *buf, size_t len, bool *eof_out) {
    *eof_out = c->stream_remote_eof && c->rx_off >= c->rx_end;
    if (*eof_out) return 0;

    if (c->rx_off < c->rx_end) {
        size_t avail = c->rx_end - c->rx_off;
        if (avail > len) avail = len;
        memcpy(buf, c->rx_buf + c->rx_off, avail);
        c->rx_off += avail;
        *eof_out = c->stream_remote_eof && c->rx_off >= c->rx_end;
        return (int)avail;
    }

    for (;;) {
        if (c->stream_remote_eof) {
            *eof_out = true;
            return c->stream_rst ? WTLS_ERR_IO : 0;
        }
        h2_frame f;
        int st = read_frame(c, &f, false, 0);
        if (st != WTLS_OK) return st;
        if (f.type == H2_DATA && f.stream == c->cur_stream_id) {
            size_t doff = 0, dlen = 0;
            st = extract_data_frame(c, &f, &doff, &dlen);
            if (st != WTLS_OK) {
                c->broken = true;
                return st;
            }
            if (dlen == 0) {
                if (c->stream_remote_eof) {
                    *eof_out = true;
                    return 0;
                }
                continue;
            }
            size_t take = dlen < len ? dlen : len;
            memcpy(buf, f.payload + doff, take);
            if (take < dlen) {
                c->rx_off = doff + take;
                c->rx_end = doff + dlen;
            } else {
                c->rx_off = c->rx_end = 0;
            }
            *eof_out = c->stream_remote_eof && c->rx_off >= c->rx_end;
            return (int)take;
        }
        if (f.type == H2_HEADERS && f.stream == c->cur_stream_id) {
            /* Trailers (END_STREAM) on an open response stream */
            if (!(f.flags & H2_FLAG_END_STREAM)) {
                c->broken = true;
                return WTLS_ERR_HTTP;
            }
            bool end_hdrs = (f.flags & H2_FLAG_END_HEADERS) != 0;
            while (!end_hdrs) {
                h2_frame cont;
                st = read_frame(c, &cont, false, 0);
                if (st != WTLS_OK) return st;
                if (cont.type != H2_CONTINUATION || cont.stream != c->cur_stream_id) {
                    c->broken = true;
                    return WTLS_ERR_HTTP;
                }
                if (cont.flags & H2_FLAG_END_HEADERS) end_hdrs = true;
            }
            c->stream_remote_eof = true;
            *eof_out = true;
            return 0;
        }
        st = handle_common_frame(c, &f);
        if (st != WTLS_OK) return st;
    }
}

bool wtls_h2_stream_eof(const wtls_h2_conn *c) {
    return c && c->stream_remote_eof && c->rx_off >= c->rx_end;
}
