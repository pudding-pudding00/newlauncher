/**
 * @file websocket.h
 * @brief WebSocket client (RFC 6455) over ws:// and wss://, built on the HTTP
 *        client's upgrade handoff.
 *
 * Same threading rule as wiitls.h. wtls_init() must have succeeded. Messages
 * are sent as one frame (client-masked) and received complete: fragments are
 * reassembled, pings answered, Close handled. No extensions (no compression).
 */
#ifndef WIITLS_WEBSOCKET_H
#define WIITLS_WEBSOCKET_H

#include <wiitls/http.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque connection. */
typedef struct wtls_ws wtls_ws;

/** Message types for wtls_ws_send() / wtls_ws_recv(). */
enum { WTLS_WS_TEXT = 1, WTLS_WS_BINARY = 2 };

/** Connection options. Zero-initialise; new fields are appended. */
typedef struct {
    const char *url;                 /**< "ws://host[:port]/path" or "wss://..." */
    const wtls_http_header *headers; /**< extra handshake headers (not Host, Upgrade, Connection, Sec-WebSocket-*) */
    size_t n_headers;                /**< <= 32 */
    const char *const *subprotocols; /**< NULL-terminated list to offer (optional) */
    const wtls_options *tls;         /**< timeouts, abort_cb, pins, client certificate; alpn is ignored */
    uint32_t max_message;            /**< 0 => 1 MiB: largest message sent or received; a larger incoming one
                                          is answered with Close 1009 and WTLS_ERR_WS */
    uint32_t ping_interval_ms;       /**< 0 => no automatic pings; otherwise a Ping is sent after this long
                                          without one, whenever the library waits or is called */
} wtls_ws_options;

/**
 * Connects and performs the handshake (blocking). On success *out is a
 * connection in blocking mode.
 *
 * @return WTLS_OK; WTLS_ERR_WS (the server did not switch protocols correctly:
 *         status, Upgrade/Connection, Sec-WebSocket-Accept, subprotocol,
 *         extensions); WTLS_ERR_ARG; or any error of wtls_http_open().
 */
wtls_status wtls_ws_connect(wtls_ws **out, const wtls_ws_options *o);

/**
 * Sends one message. Blocking mode returns @p len or a negative status.
 * Non-blocking mode may return WTLS_WANT_WRITE: call again with the same
 * arguments until it returns @p len.
 *
 * @return @p len; WTLS_WANT_WRITE; WTLS_ERR_HTTP_LIMIT (over max_message);
 *         WTLS_ERR_STATE (Close already sent); WTLS_ERR_CLOSED (peer closed);
 *         WTLS_ERR_TIMEOUT; WTLS_ERR_IO; WTLS_ERR_ARG.
 */
int wtls_ws_send(wtls_ws *ws, int type, const void *data, size_t len);

/**
 * Receives one complete message into @p buf and returns its length (possibly 0)
 * with *type set. Blocking mode waits up to the I/O timeout
 * (WTLS_ERR_TIMEOUT; the connection stays usable). Non-blocking mode returns
 * WTLS_WANT_READ. Ping, Pong and Close frames are handled internally.
 *
 * @return length >= 0; WTLS_WANT_READ; WTLS_ERR_CLOSED (the peer sent Close:
 *         see wtls_ws_close_code(); also on every later call); WTLS_ERR_WS
 *         (protocol violation, Close was sent); WTLS_ERR_HTTP_LIMIT (@p cap is
 *         too small: the message stays queued, call again with a bigger buffer);
 *         WTLS_ERR_TIMEOUT; WTLS_ERR_IO; WTLS_ERR_CANCELLED.
 */
int wtls_ws_recv(wtls_ws *ws, int *type, void *buf, size_t cap);

/** Queues a Ping with up to 125 bytes of payload. WTLS_OK, WTLS_WANT_WRITE (try again), or an error. */
wtls_status wtls_ws_ping(wtls_ws *ws, const void *data, size_t len);

/** The subprotocol the server chose, or NULL. */
const char *wtls_ws_subprotocol(const wtls_ws *ws);

/** Close status code of the peer's Close frame (1005 if it had none), 0 before one arrived. */
uint16_t wtls_ws_close_code(const wtls_ws *ws);

/** Switches wtls_ws_send()/wtls_ws_recv() between blocking (default) and non-blocking. */
wtls_status wtls_ws_set_nonblocking(wtls_ws *ws, bool on);

/** Socket for your own net_poll(); check wtls_ws_pending() first. -1 for NULL. */
int wtls_ws_get_fd(const wtls_ws *ws);

/** Bytes already received but not yet processed (0 means waiting on the socket is enough). */
size_t wtls_ws_pending(const wtls_ws *ws);

/**
 * Sends Close (@p code 0 = none, otherwise 1000-1003, 1007-1011 or 3000-4999;
 * @p reason up to 123 bytes, may be NULL), waits up to 1 s for the peer's Close, and frees
 * the connection. NULL is allowed.
 */
void wtls_ws_close(wtls_ws *ws, uint16_t code, const char *reason);

#ifdef __cplusplus
}
#endif

#endif
