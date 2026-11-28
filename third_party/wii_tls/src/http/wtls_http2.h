/* HTTP/2 client connection and sequential stream engine (RFC 9113, not installed). */
#ifndef WTLS_HTTP2_H
#define WTLS_HTTP2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <wiitls/http.h>
#include <wiitls/wiitls.h>

#include "http/wtls_hpack.h"
#include "http/wtls_http_parse.h"
#include "http/wtls_url.h"

typedef struct wtls_h2_conn wtls_h2_conn;

/**
 * Byte transport under an HTTP/2 connection. The TLS transport is built in;
 * wtls_h2_create_io() takes another one so the frame engine can be driven by
 * the fuzzers without a socket.
 */
typedef struct {
    /** Blocking read of >= 1 byte. @p max_wait_ms 0 = the transport's own I/O timeout, otherwise
     *  wait at most that long. Returns > 0 bytes or a negative wtls_status (WTLS_ERR_CLOSED = EOF). */
    int (*read)(void *ctx, void *buf, size_t len, uint32_t max_wait_ms);
    /** Writes all of buf or fails: WTLS_OK or a negative wtls_status. */
    int (*write)(void *ctx, const void *buf, size_t len);
    /** True when a read would return data without waiting (idle liveness check). */
    bool (*readable)(void *ctx);
    /** One read that never waits: > 0 bytes, WTLS_WANT_READ, or <= 0 on failure. */
    int (*read_nowait)(void *ctx, void *buf, size_t len);
} wtls_h2_io;

/** Like wtls_h2_create() over a caller-supplied transport (which must outlive the connection). */
int wtls_h2_create_io(const wtls_h2_io *io, void *io_ctx, wtls_h2_conn **out);

/**
 * Allocates an HTTP/2 connection over an established TLS session (ALPN "h2"),
 * sending the RFC 9113 client connection preface, SETTINGS, and connection WINDOW_UPDATE.
 */
int wtls_h2_create(wtls_conn *tls, wtls_h2_conn **out);

/** Releases an HTTP/2 connection (does not close tls itself). */
void wtls_h2_free(wtls_h2_conn *c);

/** True if the connection has not seen GOAWAY or a fatal error. */
bool wtls_h2_reusable(const wtls_h2_conn *c);

/** Processes any pending idle control frames without blocking; returns true if alive and usable. */
bool wtls_h2_alive(wtls_h2_conn *c);

/** Opens the next client stream (1, 3, 5, ...) and resets per-stream state. */
int wtls_h2_begin_stream(wtls_h2_conn *c);

/** Sends request HEADERS frame for the current stream; sets END_STREAM when !has_body. */
int wtls_h2_send_headers(wtls_h2_conn *c, const char *method, const wtls_url *u,
                         const wtls_http_header *h, size_t n_headers,
                         bool has_content_length, size_t body_len, bool has_body,
                         bool want_gzip, bool expect_100);

/** Sends request body DATA (respecting connection & stream send windows) and END_STREAM if end_stream. */
int wtls_h2_send_data(wtls_h2_conn *c, const uint8_t *data, size_t len, bool end_stream,
                      bool *early_final, wtls_http_head *head_out, char **arena_out, bool *got_head);

/** Waits up to 1000 ms for 100 Continue (returns early_final=true if >= 200 arrived instead). */
int wtls_h2_wait_continue(wtls_h2_conn *c, uint32_t continue_timeout_ms, uint32_t header_timeout_ms,
                          bool *early_final, wtls_http_head *head_out, char **arena_out, bool *got_head);

/**
 * Reads frames on the current stream until a final response HEADERS block (>= 200) arrives.
 * Caller owns *arena_out on success.
 */
int wtls_h2_read_head(wtls_h2_conn *c, uint32_t header_timeout_ms, wtls_http_head *head_out,
                      char **arena_out, bool *got_head);

/**
 * Reads up to len decoded DATA bytes for the current stream; returns 0 on END_STREAM,
 * >0 for bytes read, or <0 on error.
 */
int wtls_h2_read_data(wtls_h2_conn *c, void *buf, size_t len, bool *eof_out);

/** True if END_STREAM has already been observed on the current stream (e.g. on HEADERS). */
bool wtls_h2_stream_eof(const wtls_h2_conn *c);

#endif
