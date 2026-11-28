/* WebSocket client connection (RFC 6455) over the HTTP client's 101 handoff. */
#include <string.h>

#include <mbedtls/base64.h>
#include <mbedtls/platform_util.h>
#include <psa/crypto.h>
#include <wiitls/websocket.h>

#include "plat/plat.h"
#include "wtls_internal.h"
#include "wtls_mem.h"
#include "ws/wtls_ws_frame.h"

#if !defined(WTLS_WEBSOCKET) || !WTLS_WEBSOCKET

/* Built with -DWTLS_WEBSOCKET=OFF: the symbols exist, nothing works. */
wtls_status wtls_ws_connect(wtls_ws **out, const wtls_ws_options *o) {
    (void)o;
    if (out) *out = NULL;
    return WTLS_ERR_UNSUPPORTED;
}
int wtls_ws_send(wtls_ws *ws, int type, const void *data, size_t len) {
    (void)ws; (void)type; (void)data; (void)len;
    return WTLS_ERR_UNSUPPORTED;
}
int wtls_ws_recv(wtls_ws *ws, int *type, void *buf, size_t cap) {
    (void)ws; (void)type; (void)buf; (void)cap;
    return WTLS_ERR_UNSUPPORTED;
}
wtls_status wtls_ws_ping(wtls_ws *ws, const void *data, size_t len) {
    (void)ws; (void)data; (void)len;
    return WTLS_ERR_UNSUPPORTED;
}
const char *wtls_ws_subprotocol(const wtls_ws *ws) { (void)ws; return NULL; }
uint16_t wtls_ws_close_code(const wtls_ws *ws) { (void)ws; return 0; }
wtls_status wtls_ws_set_nonblocking(wtls_ws *ws, bool on) { (void)ws; (void)on; return WTLS_ERR_UNSUPPORTED; }
int wtls_ws_get_fd(const wtls_ws *ws) { (void)ws; return -1; }
size_t wtls_ws_pending(const wtls_ws *ws) { (void)ws; return 0; }
void wtls_ws_close(wtls_ws *ws, uint16_t code, const char *reason) { (void)ws; (void)code; (void)reason; }

#else

#define DEFAULT_MAX_MESSAGE (1024u * 1024u)
#define DEFAULT_IO_MS 15000u
#define RX_SIZE 2048u
#define CLOSE_WAIT_MS 1000u
#define SUBPROTO_MAX 64u
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

struct wtls_ws {
    wtls_conn *tls; /* wss */
    int fd;         /* ws or the TLS socket */
    bool nonblocking;
    uint32_t io_timeout_ms, max_message, ping_interval_ms;
    wtls_abort_cb abort_cb;
    void *abort_ctx;
    char subprotocol[SUBPROTO_MAX];
    /* receive */
    uint8_t rx[RX_SIZE];
    size_t rx_off, rx_len;
    wtls_ws_hdr fh;
    bool in_frame;
    uint64_t frame_got;
    uint8_t *msg;
    size_t msg_len, msg_cap;
    int msg_type;
    bool msg_started, msg_complete;
    uint8_t ctl[WTLS_WS_MAX_CONTROL];
    size_t ctl_len;
    /* send */
    uint8_t out_ctl[WTLS_WS_MAX_HDR + WTLS_WS_MAX_CONTROL];
    size_t out_ctl_len, out_ctl_off;
    uint8_t pong[WTLS_WS_MAX_CONTROL];
    size_t pong_len;
    bool pong_pending;
    uint8_t *tx;
    size_t tx_len, tx_off;
    /* state */
    bool close_sent, close_recv, failed;
    uint16_t close_code;
    uint64_t last_ping_ms;
};

/* ---- transport ----------------------------------------------------------- */

static int t_read(wtls_ws *ws, void *buf, size_t len) {
    if (ws->tls) return wtls_read(ws->tls, buf, len);
    int r = wtls_plat_recv(ws->fd, buf, len);
    return r == 0 ? WTLS_ERR_CLOSED : r;
}

static int t_write(wtls_ws *ws, const void *buf, size_t len) {
    if (ws->tls) return wtls_write(ws->tls, buf, len);
    return wtls_plat_send(ws->fd, buf, len);
}

/* Waits until the socket is readable/writable, the deadline passes, or abort_cb says stop. */
static int t_wait(wtls_ws *ws, bool rd, bool wr, uint64_t deadline);

static int random_bytes(uint8_t *p, size_t n) {
    return psa_generate_random(p, n) == PSA_SUCCESS ? WTLS_OK : WTLS_ERR_ENTROPY;
}

/* ---- control frames out --------------------------------------------------- */

static void queue_ctl(wtls_ws *ws, uint8_t opcode, const uint8_t *payload, size_t n) {
    uint8_t mask[4];
    if (random_bytes(mask, 4) != WTLS_OK) memset(mask, 0x5a, 4); /* unpredictability is not a security property of the control path */
    size_t h = wtls_ws_build_header(ws->out_ctl, true, opcode, n, mask);
    if (n) {
        memcpy(ws->out_ctl + h, payload, n);
        wtls_ws_mask(ws->out_ctl + h, n, mask, 0);
    }
    ws->out_ctl_len = h + n;
    ws->out_ctl_off = 0;
}

/* Writes what is pending in the control slots. WTLS_OK (nothing left) or WTLS_WANT_WRITE / an error. */
static int flush_ctl(wtls_ws *ws) {
    for (;;) {
        if (ws->out_ctl_off < ws->out_ctl_len) {
            int k = t_write(ws, ws->out_ctl + ws->out_ctl_off, ws->out_ctl_len - ws->out_ctl_off);
            if (k == WTLS_WANT_WRITE || k == WTLS_WANT_READ) return WTLS_WANT_WRITE;
            if (k <= 0) return k == 0 ? WTLS_ERR_IO : k;
            ws->out_ctl_off += (size_t)k;
            continue;
        }
        ws->out_ctl_off = ws->out_ctl_len = 0;
        if (ws->pong_pending && !ws->close_sent) {
            queue_ctl(ws, WTLS_WS_OP_PONG, ws->pong, ws->pong_len);
            ws->pong_pending = false;
            continue;
        }
        return WTLS_OK;
    }
}

static bool ctl_idle(const wtls_ws *ws) { return ws->out_ctl_len == 0 && !ws->pong_pending; }

static void queue_close(wtls_ws *ws, uint16_t code, const char *reason) {
    uint8_t p[2 + 123];
    size_t n = 0;
    if (code) {
        p[n++] = (uint8_t)(code >> 8);
        p[n++] = (uint8_t)code;
        size_t rl = reason ? strlen(reason) : 0;
        if (rl > 123) rl = 123;
        if (rl) memcpy(p + n, reason, rl);
        n += rl;
    }
    ws->pong_pending = false; /* nothing follows a Close */
    queue_ctl(ws, WTLS_WS_OP_CLOSE, p, n);
    ws->close_sent = true;
}

/* Protocol violation: tell the peer and fail the connection. */
static int fail_ws(wtls_ws *ws, uint16_t code) {
    if (!ws->close_sent) {
        ws->out_ctl_len = ws->out_ctl_off = 0; /* a half-sent control frame is abandoned with the connection */
        queue_close(ws, code, NULL);
        (void)flush_ctl(ws); /* best effort */
    }
    ws->failed = true;
    return WTLS_ERR_WS;
}

static void maybe_auto_ping(wtls_ws *ws) {
    if (!ws->ping_interval_ms || ws->close_sent || !ctl_idle(ws)) return;
    uint64_t now = wtls_plat_now_ms();
    if (now - ws->last_ping_ms < ws->ping_interval_ms) return;
    ws->last_ping_ms = now;
    queue_ctl(ws, WTLS_WS_OP_PING, NULL, 0);
    (void)flush_ctl(ws);
}

static int t_wait(wtls_ws *ws, bool rd, bool wr, uint64_t deadline) {
    for (;;) {
        if (ws->abort_cb && ws->abort_cb(ws->abort_ctx)) return WTLS_ERR_CANCELLED;
        uint64_t now = wtls_plat_now_ms();
        if (now >= deadline) return WTLS_ERR_TIMEOUT;
        maybe_auto_ping(ws);
        uint64_t left = deadline - now;
        uint32_t slice = left > WTLS_POLL_SLICE_MS ? WTLS_POLL_SLICE_MS : (uint32_t)left;
        if (ws->ping_interval_ms && slice > ws->ping_interval_ms) slice = ws->ping_interval_ms;
        if (rd && ws->tls && wtls_pending(ws->tls)) return WTLS_OK;
        int p = wtls_plat_poll(ws->fd, rd, wr, slice);
        if (p < 0) return WTLS_ERR_IO;
        if (p > 0) return WTLS_OK;
    }
}

/* ---- receive --------------------------------------------------------------- */

static int fill_rx(wtls_ws *ws) {
    if (ws->rx_off == ws->rx_len) ws->rx_off = ws->rx_len = 0;
    else if (ws->rx_off > 0) {
        memmove(ws->rx, ws->rx + ws->rx_off, ws->rx_len - ws->rx_off);
        ws->rx_len -= ws->rx_off;
        ws->rx_off = 0;
    }
    int k = t_read(ws, ws->rx + ws->rx_len, sizeof ws->rx - ws->rx_len);
    if (k == WTLS_WANT_WRITE) return WTLS_WANT_READ;
    if (k <= 0) return k == 0 ? WTLS_ERR_IO : k;
    ws->rx_len += (size_t)k;
    return k;
}

static bool grow_msg(wtls_ws *ws, size_t need) {
    if (need <= ws->msg_cap) return true;
    size_t ncap = ws->msg_cap ? ws->msg_cap : 4096u;
    while (ncap < need) ncap *= 2;
    if (ncap > ws->max_message) ncap = ws->max_message;
    uint8_t *nm = wtls_realloc(ws->msg, ncap ? ncap : 1);
    if (!nm) return false;
    ws->msg = nm;
    ws->msg_cap = ncap;
    return true;
}

/* A complete control frame arrived in ws->ctl. WTLS_OK to go on, WTLS_ERR_CLOSED after Close, or an error. */
static int handle_control(wtls_ws *ws) {
    switch (ws->fh.opcode) {
    case WTLS_WS_OP_PING:
        memcpy(ws->pong, ws->ctl, ws->ctl_len); /* only the latest ping needs an answer */
        ws->pong_len = ws->ctl_len;
        ws->pong_pending = !ws->close_sent;
        (void)flush_ctl(ws);
        return WTLS_OK;
    case WTLS_WS_OP_PONG:
        return WTLS_OK;
    default: { /* Close */
        uint16_t code = 1005;
        if (ws->ctl_len == 1) return fail_ws(ws, 1002);
        if (ws->ctl_len >= 2) {
            code = (uint16_t)((ws->ctl[0] << 8) | ws->ctl[1]);
            if (!wtls_ws_close_code_valid(code)) return fail_ws(ws, 1002);
            if (!wtls_ws_valid_utf8(ws->ctl + 2, ws->ctl_len - 2)) return fail_ws(ws, 1007);
        }
        ws->close_recv = true;
        ws->close_code = code;
        if (!ws->close_sent) {
            ws->out_ctl_len = ws->out_ctl_off = 0;
            queue_close(ws, code == 1005 ? 0 : code, NULL);
            (void)flush_ctl(ws);
        }
        return WTLS_ERR_CLOSED;
    }
    }
}

/* Consumes input until a data message is complete (WTLS_OK), more input is needed
 * (WTLS_WANT_READ), the peer closed (WTLS_ERR_CLOSED) or something failed. */
static int pump(wtls_ws *ws) {
    for (;;) {
        if (!ws->in_frame) {
            int r = wtls_ws_parse_header(ws->rx + ws->rx_off, ws->rx_len - ws->rx_off, &ws->fh);
            if (r == WTLS_ERR_WS) return fail_ws(ws, 1002);
            if (r == WTLS_WANT_READ) {
                int k = fill_rx(ws);
                if (k < 0) return k;
                continue;
            }
            if (ws->fh.masked) return fail_ws(ws, 1002); /* servers never mask */
            uint8_t op = ws->fh.opcode;
            if (op == WTLS_WS_OP_CONT && !ws->msg_started) return fail_ws(ws, 1002);
            if ((op == WTLS_WS_OP_TEXT || op == WTLS_WS_OP_BINARY) && ws->msg_started) return fail_ws(ws, 1002);
            if (op < 8 && (ws->fh.len > ws->max_message || ws->msg_len + ws->fh.len > ws->max_message))
                return fail_ws(ws, 1009);
            if (op == WTLS_WS_OP_TEXT || op == WTLS_WS_OP_BINARY) {
                ws->msg_type = op == WTLS_WS_OP_TEXT ? WTLS_WS_TEXT : WTLS_WS_BINARY;
                ws->msg_started = true;
                ws->msg_len = 0;
            }
            ws->ctl_len = 0;
            ws->rx_off += ws->fh.hdr_len;
            ws->in_frame = true;
            ws->frame_got = 0;
        }
        uint64_t need = ws->fh.len - ws->frame_got;
        size_t avail = ws->rx_len - ws->rx_off;
        if (need > 0 && avail == 0) {
            int k = fill_rx(ws);
            if (k < 0) return k;
            continue;
        }
        size_t take = need < avail ? (size_t)need : avail;
        if (ws->fh.opcode >= 8) {
            memcpy(ws->ctl + ws->ctl_len, ws->rx + ws->rx_off, take);
            ws->ctl_len += take;
        } else if (take) {
            if (!grow_msg(ws, ws->msg_len + take)) return WTLS_ERR_NOMEM;
            memcpy(ws->msg + ws->msg_len, ws->rx + ws->rx_off, take);
            ws->msg_len += take;
        }
        ws->rx_off += take;
        ws->frame_got += take;
        if (ws->frame_got < ws->fh.len) continue;
        ws->in_frame = false;
        if (ws->fh.opcode >= 8) {
            int r = handle_control(ws);
            if (r != WTLS_OK) return r;
            continue;
        }
        if (!ws->fh.fin) continue;
        ws->msg_started = false;
        if (ws->msg_type == WTLS_WS_TEXT && !wtls_ws_valid_utf8(ws->msg, ws->msg_len)) return fail_ws(ws, 1007);
        ws->msg_complete = true;
        return WTLS_OK;
    }
}

int wtls_ws_recv(wtls_ws *ws, int *type, void *buf, size_t cap) {
    if (!ws || !type || (!buf && cap)) return WTLS_ERR_ARG;
    if (ws->failed) return WTLS_ERR_WS;
    uint64_t deadline = wtls_plat_now_ms() + ws->io_timeout_ms;
    while (!ws->msg_complete) {
        if (ws->close_recv) return WTLS_ERR_CLOSED;
        int r = pump(ws);
        if (r == WTLS_WANT_READ) {
            if (ws->nonblocking) return WTLS_WANT_READ;
            r = t_wait(ws, true, false, deadline);
            if (r != WTLS_OK) return r;
            continue;
        }
        if (r == WTLS_ERR_IO && ws->close_recv) return WTLS_ERR_CLOSED;
        if (r != WTLS_OK) return r;
    }
    if (ws->msg_len > cap) return WTLS_ERR_HTTP_LIMIT;
    int n = (int)ws->msg_len;
    if (n) memcpy(buf, ws->msg, ws->msg_len);
    *type = ws->msg_type;
    ws->msg_complete = false;
    ws->msg_len = 0;
    return n;
}

/* ---- send ------------------------------------------------------------------- */

static int flush_tx(wtls_ws *ws, uint64_t deadline) {
    for (;;) {
        int r = flush_ctl(ws);
        if (r == WTLS_OK && ws->tx_off < ws->tx_len) {
            int k = t_write(ws, ws->tx + ws->tx_off, ws->tx_len - ws->tx_off);
            if (k > 0) {
                ws->tx_off += (size_t)k;
                if (ws->tx_off == ws->tx_len) {
                    wtls_free(ws->tx);
                    ws->tx = NULL;
                    ws->tx_len = ws->tx_off = 0;
                    return WTLS_OK;
                }
                continue;
            }
            r = (k == WTLS_WANT_WRITE || k == WTLS_WANT_READ) ? WTLS_WANT_WRITE : (k == 0 ? WTLS_ERR_IO : k);
        } else if (r == WTLS_OK) {
            return WTLS_OK;
        }
        if (r != WTLS_WANT_WRITE) return r;
        if (ws->nonblocking) return WTLS_WANT_WRITE;
        r = t_wait(ws, false, true, deadline);
        if (r != WTLS_OK) return r;
    }
}

int wtls_ws_send(wtls_ws *ws, int type, const void *data, size_t len) {
    if (!ws || (!data && len) || (type != WTLS_WS_TEXT && type != WTLS_WS_BINARY)) return WTLS_ERR_ARG;
    if (ws->failed) return WTLS_ERR_WS;
    if (ws->close_recv) return WTLS_ERR_CLOSED;
    if (ws->close_sent) return WTLS_ERR_STATE;
    if (len > ws->max_message) return WTLS_ERR_HTTP_LIMIT;
    if (!ws->tx) { /* a frame left over from a WTLS_WANT_WRITE is resumed instead */
        uint8_t mask[4];
        int r = random_bytes(mask, 4);
        if (r != WTLS_OK) return r;
        uint8_t hdr[WTLS_WS_MAX_HDR];
        size_t h = wtls_ws_build_header(hdr, true, type == WTLS_WS_TEXT ? WTLS_WS_OP_TEXT : WTLS_WS_OP_BINARY, len, mask);
        ws->tx = wtls_malloc(h + len);
        if (!ws->tx) return WTLS_ERR_NOMEM;
        memcpy(ws->tx, hdr, h);
        if (len) {
            memcpy(ws->tx + h, data, len);
            wtls_ws_mask(ws->tx + h, len, mask, 0);
        }
        ws->tx_len = h + len;
        ws->tx_off = 0;
    }
    int r = flush_tx(ws, wtls_plat_now_ms() + ws->io_timeout_ms);
    if (r != WTLS_OK) {
        if (r != WTLS_WANT_WRITE && ws->tx) { /* a failed blocking send abandons the frame: the stream is unusable */
            wtls_free(ws->tx);
            ws->tx = NULL;
            ws->tx_len = ws->tx_off = 0;
        }
        return r;
    }
    return (int)len;
}

wtls_status wtls_ws_ping(wtls_ws *ws, const void *data, size_t len) {
    if (!ws || (!data && len) || len > WTLS_WS_MAX_CONTROL) return WTLS_ERR_ARG;
    if (ws->failed) return WTLS_ERR_WS;
    if (ws->close_sent) return WTLS_ERR_STATE;
    if (!ctl_idle(ws) && flush_ctl(ws) != WTLS_OK) return WTLS_WANT_WRITE;
    if (!ctl_idle(ws)) return WTLS_WANT_WRITE;
    queue_ctl(ws, WTLS_WS_OP_PING, data, len);
    int r = flush_ctl(ws);
    return r == WTLS_OK || r == WTLS_WANT_WRITE ? WTLS_OK : (wtls_status)r;
}

/* ---- accessors ---------------------------------------------------------------- */

const char *wtls_ws_subprotocol(const wtls_ws *ws) { return ws && ws->subprotocol[0] ? ws->subprotocol : NULL; }
uint16_t wtls_ws_close_code(const wtls_ws *ws) { return ws ? ws->close_code : 0; }
int wtls_ws_get_fd(const wtls_ws *ws) { return ws ? ws->fd : -1; }

wtls_status wtls_ws_set_nonblocking(wtls_ws *ws, bool on) {
    if (!ws) return WTLS_ERR_ARG;
    ws->nonblocking = on;
    return WTLS_OK;
}

size_t wtls_ws_pending(const wtls_ws *ws) {
    if (!ws) return 0;
    size_t n = ws->rx_len - ws->rx_off + (ws->msg_complete ? 1u : 0u);
    if (ws->tls) n += wtls_pending(ws->tls);
    return n;
}

/* ---- close -------------------------------------------------------------------- */

void wtls_ws_close(wtls_ws *ws, uint16_t code, const char *reason) {
    if (!ws) return;
    uint64_t deadline = wtls_plat_now_ms() + CLOSE_WAIT_MS;
    ws->nonblocking = true; /* we wait ourselves, bounded by the deadline */
    ws->abort_cb = NULL;
    if (!ws->failed && !ws->close_sent && !ws->close_recv) {
        if (code && !wtls_ws_close_code_valid(code)) code = 1000;
        if (ws->tx) { /* a half-written data frame: the peer's parser would be confused, just drop the link */
            wtls_free(ws->tx);
            ws->tx = NULL;
        } else {
            ws->out_ctl_len = ws->out_ctl_off = 0;
            queue_close(ws, code, reason);
        }
    }
    while (!ws->failed && ws->close_sent && flush_ctl(ws) == WTLS_WANT_WRITE) {
        if (t_wait(ws, false, true, deadline) != WTLS_OK) break;
    }
    while (!ws->failed && ws->close_sent && !ws->close_recv) { /* the peer's Close ends the closing handshake */
        int r = pump(ws);
        ws->msg_complete = false;
        ws->msg_len = 0;
        if (r == WTLS_WANT_READ) {
            if (t_wait(ws, true, false, deadline) != WTLS_OK) break;
            continue;
        }
        if (r != WTLS_OK) break;
    }
    if (ws->tls) wtls_close(ws->tls);
    else if (ws->fd >= 0) wtls_plat_close(ws->fd);
    wtls_free(ws->msg);
    wtls_free(ws->tx);
    mbedtls_platform_zeroize(ws, sizeof *ws);
    wtls_free(ws);
}

/* ---- handshake ----------------------------------------------------------------- */

static bool ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        char x = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char y = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (x != y) return false;
    }
    return *a == *b;
}

/* true if the comma-separated header value contains the token (case-insensitive). */
static bool has_token(const char *v, const char *token) {
    size_t tl = strlen(token);
    while (*v) {
        while (*v == ' ' || *v == '\t' || *v == ',') v++;
        const char *s = v;
        while (*v && *v != ',') v++;
        const char *e = v;
        while (e > s && (e[-1] == ' ' || e[-1] == '\t')) e--;
        if ((size_t)(e - s) == tl) {
            size_t i = 0;
            while (i < tl && ((s[i] | 0x20) == (token[i] | 0x20))) i++;
            if (i == tl) return true;
        }
    }
    return false;
}

static bool compute_accept(const char *key_b64, char out[32]) {
    uint8_t in[24 + sizeof WS_GUID - 1], digest[20];
    size_t kl = strlen(key_b64), dl = 0;
    memcpy(in, key_b64, kl);
    memcpy(in + kl, WS_GUID, sizeof WS_GUID - 1);
    if (psa_hash_compute(PSA_ALG_SHA_1, in, kl + sizeof WS_GUID - 1, digest, sizeof digest, &dl) != PSA_SUCCESS || dl != 20)
        return false;
    size_t ol = 0;
    return mbedtls_base64_encode((unsigned char *)out, 32, &ol, digest, 20) == 0 && ol < 32 && (out[ol] = 0, true);
}

static bool forbidden_header(const char *n) {
    static const char *const bad[] = {"host", "upgrade", "connection", "sec-websocket-key", "sec-websocket-version",
                                      "sec-websocket-accept", "sec-websocket-extensions", "sec-websocket-protocol",
                                      "content-length", "transfer-encoding"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        if (ieq(n, bad[i])) return true;
    return false;
}

wtls_status wtls_ws_connect(wtls_ws **out, const wtls_ws_options *o) {
    if (!out) return WTLS_ERR_ARG;
    *out = NULL;
    if (!o || !o->url || o->n_headers > 32 || (o->n_headers && !o->headers)) return WTLS_ERR_ARG;
    if (!wtls_core_initialized()) return WTLS_ERR_STATE;
    bool secure;
    const char *rest;
    if (!strncmp(o->url, "wss://", 6)) { secure = true; rest = o->url + 6; }
    else if (!strncmp(o->url, "ws://", 5)) { secure = false; rest = o->url + 5; }
    else return WTLS_ERR_ARG;
    for (size_t i = 0; i < o->n_headers; i++) {
        if (!o->headers[i].name || !o->headers[i].value || forbidden_header(o->headers[i].name)) return WTLS_ERR_ARG;
    }
    size_t rl = strlen(rest);
    char *http_url = wtls_malloc(rl + 9);
    wtls_http_header *hdrs = wtls_calloc(o->n_headers + 4, sizeof *hdrs);
    char *protos = NULL;
    wtls_ws *ws = wtls_calloc(1, sizeof *ws);
    wtls_status st = WTLS_ERR_NOMEM;
    wtls_http_response *resp = NULL;
    if (!http_url || !hdrs || !ws) goto out;
    memcpy(http_url, secure ? "https://" : "http://", secure ? 8 : 7);
    memcpy(http_url + (secure ? 8 : 7), rest, rl + 1);

    uint8_t keyraw[16];
    char key[32];
    size_t kl = 0;
    st = (wtls_status)random_bytes(keyraw, sizeof keyraw);
    if (st != WTLS_OK) goto out;
    if (mbedtls_base64_encode((unsigned char *)key, sizeof key - 1, &kl, keyraw, sizeof keyraw) != 0) {
        st = WTLS_ERR_INTERNAL;
        goto out;
    }
    key[kl] = 0;

    size_t nh = 0;
    for (size_t i = 0; i < o->n_headers; i++) hdrs[nh++] = o->headers[i];
    hdrs[nh++] = (wtls_http_header){"Upgrade", "websocket"};
    hdrs[nh++] = (wtls_http_header){"Sec-WebSocket-Key", key};
    hdrs[nh++] = (wtls_http_header){"Sec-WebSocket-Version", "13"};
    size_t n_with_proto = nh;
    if (o->subprotocols && o->subprotocols[0]) {
        size_t total = 1;
        for (const char *const *p = o->subprotocols; *p; p++) total += strlen(*p) + 2;
        protos = wtls_malloc(total);
        if (!protos) goto out;
        protos[0] = 0;
        for (const char *const *p = o->subprotocols; *p; p++) {
            if (p != o->subprotocols) strcat(protos, ", ");
            strcat(protos, *p);
        }
        hdrs[n_with_proto++] = (wtls_http_header){"Sec-WebSocket-Protocol", protos};
    }
    wtls_http_request req = {0};
    req.url = http_url;
    req.headers = hdrs;
    req.n_headers = n_with_proto;
    req.max_redirects = -1; /* a redirect to another host is not something to follow blindly */
    req.no_keepalive = false;
    req.allow_upgrade = true;
    req.tls = o->tls;
    st = wtls_http_open(&resp, &req);
    if (st != WTLS_OK) goto out;

    st = WTLS_ERR_WS;
    const char *up = wtls_http_get_header(resp, "upgrade");
    const char *conn = wtls_http_get_header(resp, "connection");
    const char *acc = wtls_http_get_header(resp, "sec-websocket-accept");
    char want[32];
    if (wtls_http_status(resp) != 101 || !up || !ieq(up, "websocket") || !conn || !has_token(conn, "upgrade") || !acc ||
        !compute_accept(key, want) || strcmp(acc, want) != 0 || wtls_http_get_header(resp, "sec-websocket-extensions"))
        goto out;
    const char *sp = wtls_http_get_header(resp, "sec-websocket-protocol");
    if (sp) {
        bool offered = false;
        for (const char *const *p = o->subprotocols; p && *p; p++) offered |= strcmp(*p, sp) == 0;
        if (!offered || strlen(sp) >= SUBPROTO_MAX) goto out;
        memcpy(ws->subprotocol, sp, strlen(sp) + 1);
    }

    ws->fd = -1;
    ws->max_message = o->max_message ? o->max_message : DEFAULT_MAX_MESSAGE;
    ws->io_timeout_ms = o->tls && o->tls->io_timeout_ms ? o->tls->io_timeout_ms : DEFAULT_IO_MS;
    ws->ping_interval_ms = o->ping_interval_ms;
    ws->abort_cb = o->tls ? o->tls->abort_cb : NULL;
    ws->abort_ctx = o->tls ? o->tls->abort_ctx : NULL;
    ws->last_ping_ms = wtls_plat_now_ms();
    if (secure) {
        st = wtls_http_detach(resp, &ws->tls, NULL, NULL, 0, NULL); /* early bytes stay inside the connection */
        if (st != WTLS_OK) goto out;
        ws->fd = wtls_get_fd(ws->tls);
        wtls_set_nonblocking(ws->tls, true);
    } else {
        size_t early = 0;
        st = wtls_http_detach(resp, NULL, &ws->fd, ws->rx, sizeof ws->rx, &early);
        if (st != WTLS_OK) {
            ws->fd = -1;
            goto out;
        }
        ws->rx_len = early;
    }
    wtls_http_close(resp);
    resp = NULL;
    *out = ws;
    ws = NULL;
    st = WTLS_OK;
out:
    if (resp) wtls_http_close(resp);
    if (ws) {
        if (ws->tls) wtls_close(ws->tls);
        wtls_free(ws);
    }
    wtls_free(protos);
    wtls_free(hdrs);
    wtls_free(http_url);
    return st;
}

#endif /* WTLS_WEBSOCKET */
