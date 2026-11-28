/* HTTP/2 client session with stream multiplexing, driven without ever waiting (RFC 9113, not installed).
 * See docs/http2-multiplexing-design.md. The blocking single-stream engine is wtls_http2.c. */
#ifndef WTLS_H2S_H
#define WTLS_H2S_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <wiitls/http.h>
#include <wiitls/wiitls.h>

#include "http/wtls_hpack.h"
#include "http/wtls_http_parse.h"
#include "http/wtls_url.h"

#ifndef WTLS_H2_MAX_STREAMS
#define WTLS_H2_MAX_STREAMS 4u
#endif
#ifndef WTLS_H2_STREAM_WINDOW
#define WTLS_H2_STREAM_WINDOW 32768u
#endif

typedef struct h2s_session h2s_session;
typedef struct h2s_stream h2s_stream;

/* The byte transport; both calls must not wait. */
typedef struct {
    int (*read)(void *ctx, void *buf, size_t len);        /* > 0 bytes | WTLS_WANT_READ | <= 0: failure (WTLS_ERR_CLOSED = end) */
    int (*write)(void *ctx, const void *buf, size_t len); /* > 0 bytes | WTLS_WANT_WRITE | failure */
} h2s_io;

/* A session: queues the client preface and SETTINGS. max_streams is the client's cap on concurrent streams
 * (1..64), stream_window the receive window and buffer of each stream (>= 16384). NULL: out of memory. */
h2s_session *h2s_create(const h2s_io *io, void *io_ctx, uint32_t max_streams, uint32_t stream_window);

/* Frees the session and its streams (their handles become invalid). Does not close the transport. */
void h2s_destroy(h2s_session *s);

/* Writes queued frames, reads and processes whatever has arrived, for every stream. WTLS_OK also when there is
 * nothing to do; otherwise the connection-level error that ended the session (sticky; every stream sees it). */
int h2s_pump(h2s_session *s);

/* More input is already waiting inside the session (a pump stopped at its work budget): pump again before polling. */
bool h2s_pending(const h2s_session *s);

/* What the session waits for: bit 0 = bytes from the peer (always), bit 1 = room to write (output pending). */
unsigned h2s_wants(const h2s_session *s);

/* No GOAWAY, no error, ids left: new streams may be opened. */
bool h2s_usable(const h2s_session *s);
bool h2s_settings_known(const h2s_session *s); /* the peer's first SETTINGS are in (its stream limit is known) */
size_t h2s_active(const h2s_session *s);
/* Streams the session would open at once now: the client cap or the server's SETTINGS_MAX_CONCURRENT_STREAMS. */
size_t h2s_capacity(const h2s_session *s);
/* The sticky error (0 if none) and whether it, or a stream's failure, is worth repeating on a new connection. */
int h2s_error(const h2s_session *s);

/* Opens a stream. WTLS_OK; WTLS_WANT_READ when all slots are busy (try again after a stream was released or
 * the server raised its limit); WTLS_ERR_IO when the session cannot take new streams (GOAWAY, error). */
int h2s_stream_open(h2s_session *s, h2s_stream **out);

/* Queues the request headers (HEADERS + CONTINUATION) and gives the stream its id (ids must go out in order, so
 * this is the moment, not h2s_stream_open). has_body false ends the request. WTLS_WANT_READ when the server's
 * concurrent stream limit is reached right now (pump and try again); WTLS_ERR_IO when the session cannot start
 * streams any more. */
int h2s_stream_send_headers(h2s_stream *st, const char *method, const wtls_url *u, const wtls_http_header *h, size_t n,
                            bool has_content_length, size_t body_len, bool has_body, bool want_gzip);

/* Queues up to len bytes of the request body as one DATA frame. Returns the bytes taken (0: flow control or a full
 * output queue says wait; end_stream is then not sent), or an error. end_stream is only honoured when all of
 * data was taken; with len == 0 it sends an empty DATA frame that ends the request. */
int h2s_stream_send_data(h2s_stream *st, const uint8_t *data, size_t len, bool end_stream);

/* The final response head (1xx is skipped): WTLS_OK once (the arena is the caller's: wtls_free it), WTLS_WANT_READ,
 * or the stream's error. */
int h2s_stream_head(h2s_stream *st, wtls_http_head *head, char **arena);

/* Body bytes: > 0, or 0 with *eof set at the end, WTLS_WANT_READ when none is there yet, or the stream's error. */
int h2s_stream_read(h2s_stream *st, void *buf, size_t len, bool *eof);

/* Bytes available to h2s_stream_read() without pumping. */
size_t h2s_stream_buffered(const h2s_stream *st);
bool h2s_stream_ended(const h2s_stream *st);      /* the whole response has arrived */
int h2s_stream_error(const h2s_stream *st);       /* 0, or why it failed */
bool h2s_stream_retryable(const h2s_stream *st);  /* failed in a way another connection may fix (GOAWAY above last id, REFUSED_STREAM) */
bool h2s_stream_request_done(const h2s_stream *st); /* the whole request (headers, body, END_STREAM) is queued */
bool h2s_stream_ready(const h2s_stream *st);      /* a step would make progress: head, data, end or failure is there */
bool h2s_stream_got_data(const h2s_stream *st);   /* the server sent something for it */

/* The application is done with the stream: an unfinished one is reset with CANCEL (this stream only), unread
 * data is credited back to the connection. The handle is invalid afterwards. */
void h2s_stream_release(h2s_stream *st);

#endif
