/**
 * @file http.h
 * @brief libwiitls HTTP/1.1 client on top of wiitls.h.
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
    bool no_gzip;                    /**< do not offer gzip; bodies are returned as sent */
    const wtls_options *tls;         /**< NULL => defaults; ALPN is always "http/1.1"; for http:// only
                                          connect_timeout_ms, io_timeout_ms, and abort_cb/abort_ctx are used */
    wtls_http_body_cb body_cb;        /**< streaming upload; mutually exclusive with body (when body_len == 0
                                           and !body_chunked, sends Content-Length: 0 without invoking body_cb) */
    void *body_ctx;                   /**< opaque pointer passed to body_cb */
    bool body_chunked;                /**< with body_cb: send Transfer-Encoding: chunked (no Content-Length;
                                          the callback ends the body by returning 0) */
    wtls_http_progress_cb progress_cb; /**< wtls_http_fetch() download progress; may be NULL */
    void *progress_ctx;                /**< opaque pointer passed to progress_cb */
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
 *  body was read to its end (wtls_http_read returned 0); NULL is allowed. */
void wtls_http_close(wtls_http_response *r);

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

#ifdef __cplusplus
}
#endif

#endif
