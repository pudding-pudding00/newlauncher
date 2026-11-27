/**
 * @file http.h
 * @brief libwiitls HTTP/2 and HTTP/1.1 client on top of wiitls.h.
 *
 * https:// (TLS through wtls_connect) and plain http://. Responses are
 * streamed; wtls_http_fetch() reads a whole body into memory. Idle
 * connections are kept in a small keep-alive pool and reused by the next
 * request to the same scheme, host and port. Redirects are followed (default
 * 5). Bodies with Content-Encoding gzip are decompressed by default.
 *
 * Same threading rule as wiitls.h: call every function from one thread.
 * wtls_init() must have succeeded. An HTTP status >= 400 is not an error:
 * wtls_http_open() returns WTLS_OK and wtls_http_status() has the code.
 */
#ifndef WIITLS_HTTP_H
#define WIITLS_HTTP_H

#include <wiitls/wiitls.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One request header field. */
typedef struct {
    const char *name;  /**< RFC 9110 token; Host, Content-Length, Transfer-Encoding and Connection are refused */
    const char *value; /**< no CR, LF or other control characters except HTAB */
} wtls_http_header;

/**
 * Request body reader callback for streaming uploads.
 *
 * Writes up to @p len bytes into @p buf, reading from your source (SD card,
 * RAM, generated on the fly) without buffering the whole body in memory.
 *
 * @return >0 bytes written, 0 at the end of the body (chunked only, see
 *         body_chunked), or a negative value to abort the request.
 */
typedef int (*wtls_http_body_cb)(void *ctx, void *buf, size_t len);

/**
 * Optional download progress callback for wtls_http_fetch().
 *
 * Called as response body bytes are received. @p done is the cumulative
 * decoded byte count so far; @p total_or_minus_one is the expected length
 * from Content-Length, or -1 if unknown (chunked, close-delimited, or gzip).
 */
typedef void (*wtls_http_progress_cb)(void *ctx, size_t done, int64_t total_or_minus_one);

/**
 * A request. Zero-initialise (`{0}`) and set what you need.
 *
 * @par Source & ABI evolution
 * Always zero-initialise before use; new fields in 1.x minor updates are
 * appended at the end with zero representing the backwards-compatible default.
 */
typedef struct {
    const char *method;              /**< NULL => "GET"; RFC 9110 token, case-sensitive */
    const char *url;                 /**< "https://host[:port][/path][?query][#fragment]" or "http://..." */
    const wtls_http_header *headers; /**< extra request headers, may be NULL */
    size_t n_headers;                /**< <= 64 */
    const void *body;                /**< request body, may be NULL; sent with Content-Length */
    size_t body_len;
    int max_redirects;               /**< 0 => 5; -1 => do not follow redirects */
    uint32_t header_timeout_ms;      /**< 0 => 15000: request sent -> response head complete */
    bool no_keepalive;               /**< send "Connection: close" and never pool the connection */
    bool no_gzip;                    /**< do not offer gzip/deflate; bodies are returned as sent */
    const wtls_options *tls;         /**< NULL => defaults; alpn is ignored (the client offers "h2" and
                                          "http/1.1", see no_h2); for http:// only connect_timeout_ms,
                                          io_timeout_ms, and abort_cb/abort_ctx are used */
    wtls_http_body_cb body_cb;        /**< streaming upload; mutually exclusive with body (when body_len == 0
                                           and !body_chunked, sends Content-Length: 0 without invoking body_cb) */
    void *body_ctx;                   /**< opaque pointer passed to body_cb */
    bool body_chunked;                /**< with body_cb: send Transfer-Encoding: chunked (no Content-Length;
                                          the callback ends the body by returning 0) */
    wtls_http_progress_cb progress_cb; /**< download progress (wtls_http_fetch() and wtls_http_read()); may be NULL */
    void *progress_ctx;                /**< opaque pointer passed to progress_cb */
    bool allow_upgrade;                /**< permit HTTP 101 Switching Protocols (for wtls_http_detach());
                                            allows Connection header, suppresses gzip, and auto-adds
                                            Connection: Upgrade when an Upgrade header is present */
    bool expect_100_continue;          /**< send Expect: 100-continue and wait up to 1000 ms for 100 Continue
                                            before sending the request body (useful with body_cb) */
    const char *proxy_url;             /**< optional "http://host[:port]" HTTP proxy (uses absolute-form
                                            for http:// and CONNECT tunnel for https://; bypasses pool) */
    bool no_h2;                        /**< disable HTTP/2 ALPN ("h2") and force HTTP/1.1 */
} wtls_http_request;

/** Opaque response handle. */
typedef struct wtls_http_response wtls_http_response;

/** Facts about a response (see wtls_http_get_info()). */
typedef struct {
    const char *url;        /**< effective URL after redirects; valid until wtls_http_close() */
    uint8_t redirects;      /**< redirects followed */
    bool reused_connection; /**< came from the keep-alive pool: no connect, no handshake */
    bool secure;            /**< https (TLS); false for http:// */
    bool gzip;              /**< the body is being decompressed from gzip */
    wtls_conn_info tls;     /**< as wtls_get_info(); all zero when !secure */
    bool deflate;           /**< the body is being decompressed from deflate */
    bool http2;             /**< response arrived over HTTP/2 (ALPN "h2") */
} wtls_http_info;

/**
 * Sends the request and reads the response head (following redirects).
 * On success *out is the response; read the body with wtls_http_read() and
 * always release it with wtls_http_close(). On failure *out is NULL.
 *
 * @return WTLS_OK, WTLS_ERR_ARG (bad URL, method or header), any connection
 *         error of wtls_connect() (including WTLS_ERR_CANCELLED), WTLS_ERR_TIMEOUT,
 *         WTLS_ERR_IO, WTLS_ERR_HTTP, WTLS_ERR_HTTP_LIMIT, WTLS_ERR_HTTP_REDIRECT.
 */
wtls_status wtls_http_open(wtls_http_response **out, const wtls_http_request *req);

/** Status code 100..599, or 0 for NULL. */
int wtls_http_status(const wtls_http_response *r);

/** Value of the first field named name (case-insensitive), or NULL. */
const char *wtls_http_get_header(const wtls_http_response *r, const char *name);

/** Field i in arrival order (names in lower case); false past the end. */
bool wtls_http_header_at(const wtls_http_response *r, size_t i, const char **name, const char **value);

/** Body length from Content-Length, or -1 if unknown (chunked, until close, gzip). */
int64_t wtls_http_content_length(const wtls_http_response *r);

/**
 * Reads body bytes. Unlike wtls_read(), the end of the body is a normal
 * event here: returns > 0 bytes, 0 at the end of the body (again on every
 * later call), or a negative wtls_status (WTLS_ERR_IO: truncated body).
 */
int wtls_http_read(wtls_http_response *r, void *buf, size_t len);

/** Effective URL, redirects, connection reuse and TLS facts. */
wtls_status wtls_http_get_info(const wtls_http_response *r, wtls_http_info *out);

/** Frees the response. The connection goes back to the pool only if the
 *  body was read to its end (wtls_http_read returned 0) and it was not
 *  detached; NULL is allowed. */
void wtls_http_close(wtls_http_response *r);

/**
 * Detaches the underlying transport from a 101 Switching Protocols response
 * (opened with wtls_http_request::allow_upgrade = true), e.g. for WebSockets.
 *
 * For https://, @p tls_out receives the owning wtls_conn * (NULL for http://).
 * If @p extra_buf and @p extra_len are both NULL on https://, any early bytes
 * received right after the 101 response head are buffered inside @p tls_out so
 * wtls_read() and wtls_pending() deliver them first.
 *
 * For http:// (or when @p extra_buf / @p extra_len are provided), @p fd_out
 * receives the non-blocking socket descriptor and @p extra_buf receives any
 * early post-head bytes (pass @p extra_buf = NULL with @p extra_len != NULL to
 * query the required byte count without detaching yet).
 *
 * After a successful detach, wtls_http_close() frees the response metadata
 * without closing or pooling the detached connection.
 *
 * @return WTLS_OK, WTLS_ERR_ARG, WTLS_ERR_STATE (not a 101 response or already
 *         detached), WTLS_ERR_HTTP_LIMIT (@p extra_cap too small, or http:// has
 *         unread bytes with @p extra_buf = NULL), or WTLS_ERR_NOMEM.
 */
wtls_status wtls_http_detach(wtls_http_response *r, wtls_conn **tls_out, int *fd_out,
                             void *extra_buf, size_t extra_cap, size_t *extra_len);

/** Result of wtls_http_fetch(). */
typedef struct {
    int status;      /**< HTTP status (0 if the response head was not received; may still be set on
                          post-head errors such as WTLS_ERR_HTTP_LIMIT) */
    char *body;      /**< NUL-terminated (NUL not counted in body_len); NULL on error */
    size_t body_len;
} wtls_http_result;

#define WTLS_HTTP_FETCH_DEFAULT_MAX (1024u * 1024u)

/** Whole request in one call; max_body 0 => WTLS_HTTP_FETCH_DEFAULT_MAX.
 *  A body over max_body is WTLS_ERR_HTTP_LIMIT. Free with wtls_http_result_free(). */
wtls_status wtls_http_fetch(const wtls_http_request *req, size_t max_body, wtls_http_result *out);

/** Frees the body of a result; NULL and empty results are fine. */
void wtls_http_result_free(wtls_http_result *res);

/** Closes all idle keep-alive connections (wtls_shutdown() does this too). */
void wtls_http_pool_flush(void);

/**
 * Percent-encodes @p in (length @p in_len) per RFC 3986: the unreserved
 * characters A-Z a-z 0-9 - _ . ~ stay as they are, every other byte becomes
 * %XX with upper case hex digits. Always NUL-terminates @p out on success.
 *
 * If @p out is NULL, writes the required byte count (excluding the NUL) to
 * *out_len and returns WTLS_OK. Otherwise @p out_size must have room for
 * that count plus one; *out_len receives the count.
 *
 * @return WTLS_OK, WTLS_ERR_ARG (NULL in or out_len), or WTLS_ERR_HTTP_LIMIT
 *         if @p out_size is too small.
 */
wtls_status wtls_http_url_encode(const char *in, size_t in_len,
                                 char *out, size_t out_size, size_t *out_len);

/** A parsed RFC 9110 Content-Range value ("bytes <start>-<end>/<total|*>"). */
typedef struct {
    uint64_t start;  /**< first byte offset (inclusive) */
    uint64_t end;    /**< last byte offset (inclusive, >= start) */
    int64_t total;   /**< total representation length (>= end + 1), or -1 if '*' */
} wtls_http_content_range;

/**
 * Parses a "bytes <start>-<end>/<total|*>" Content-Range value (as sent with
 * a 206 Partial Content response, for resumable downloads). Rejects
 * unsatisfied ranges (a star in place of the range), start > end, end >=
 * total, numbers that do not fit, a missing space after "bytes" and
 * trailing garbage; leading and trailing spaces are tolerated.
 *
 * @return WTLS_OK, WTLS_ERR_ARG (NULL), or WTLS_ERR_HTTP (malformed or
 *         unsupported).
 */
wtls_status wtls_http_parse_content_range(const char *value, wtls_http_content_range *out);

/** One key/value pair for wtls_http_form_encode(). */
typedef struct {
    const char *name;
    const char *value;
} wtls_http_form_field;

/**
 * Encodes @p fields[0..n_fields) as an application/x-www-form-urlencoded body
 * ("name=value&..."), using RFC 3986 percent-encoding. Always NUL-terminates
 * @p out on success. Pass @p out = NULL to query the required length in
 * @p *out_len.
 *
 * @return WTLS_OK, WTLS_ERR_ARG, or WTLS_ERR_HTTP_LIMIT if @p out_size is
 *         too small.
 */
wtls_status wtls_http_form_encode(const wtls_http_form_field *fields, size_t n_fields,
                                  char *out, size_t out_size, size_t *out_len);

/** One part for streaming multipart/form-data uploads (see wtls_http_multipart_init()). */
typedef struct {
    const char *name;          /**< form field name (required, no CR/LF/quote) */
    const char *filename;      /**< NULL for plain form fields; filename for file uploads */
    const char *content_type;  /**< optional MIME type (defaults to application/octet-stream when filename != NULL) */
    const void *data;          /**< in-memory part bytes (mutually exclusive with data_cb) */
    size_t data_len;
    wtls_http_body_cb data_cb; /**< streaming part reader; ends part by returning 0 */
    void *data_ctx;
} wtls_http_multipart_part;

/** Streaming multipart/form-data state (use with body_cb = wtls_http_multipart_read, body_chunked = true). */
typedef struct {
    const wtls_http_multipart_part *parts;
    size_t n_parts;
    char boundary[48];
    char ct_header[80];
    size_t idx;
    int phase;
    size_t off;
    char hdr[384];
    size_t hdr_len;
} wtls_http_multipart;

/**
 * Initialises @p mp over @p parts[0..n_parts). Populates @p mp->ct_header with
 * the "multipart/form-data; boundary=..." Content-Type value. @p parts and its
 * pointed-to buffers must stay valid while wtls_http_open() streams the body.
 *
 * @return WTLS_OK or WTLS_ERR_ARG.
 */
wtls_status wtls_http_multipart_init(wtls_http_multipart *mp,
                                     const wtls_http_multipart_part *parts, size_t n_parts);

/** Returns @p mp->ct_header ("multipart/form-data; boundary=..."), or NULL. */
const char *wtls_http_multipart_content_type(const wtls_http_multipart *mp);

/** wtls_http_body_cb implementation for streaming @p wtls_http_multipart. */
int wtls_http_multipart_read(void *ctx, void *buf, size_t len);

/**
 * Formats an HTTP Range header value ("bytes=<start>-" when @p has_end is
 * false, or "bytes=<start>-<end>" when @p has_end is true).
 *
 * @return WTLS_OK, WTLS_ERR_ARG (NULL or start > end), or WTLS_ERR_HTTP_LIMIT.
 */
wtls_status wtls_http_format_range(uint64_t start, uint64_t end, bool has_end,
                                   char *out, size_t out_size);

/**
 * Evaluates a response for a Range-based download resume starting at
 * @p resume_from bytes.
 *
 * - HTTP 206 with valid Content-Range starting at @p resume_from: sets
 *   *append_offset = resume_from and *total_len = cr.total (or -1), returns WTLS_OK.
 * - HTTP 200 (server ignored Range and sent from byte 0): sets
 *   *append_offset = 0 and *total_len = wtls_http_content_length(r), returns WTLS_OK.
 * - Any other status (e.g. 416) or mismatched/malformed Content-Range:
 *   returns WTLS_ERR_HTTP.
 */
wtls_status wtls_http_check_resume(const wtls_http_response *r, uint64_t resume_from,
                                   uint64_t *append_offset, int64_t *total_len);

#ifdef __cplusplus
}
#endif

#endif
