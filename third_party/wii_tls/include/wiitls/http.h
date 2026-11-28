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

/** Opaque in-memory cookie jar, see wtls_http_request::cookie_jar. */
typedef struct wtls_cookie_jar wtls_cookie_jar;

/** Creates a jar holding at most @p max_cookies cookies (0 => 64; the oldest is evicted). NULL on no memory. */
wtls_cookie_jar *wtls_cookie_jar_new(size_t max_cookies);
/** Frees the jar and every cookie in it. NULL is allowed. */
void wtls_cookie_jar_free(wtls_cookie_jar *jar);
/** Drops all cookies (log out). */
void wtls_cookie_jar_clear(wtls_cookie_jar *jar);
/** Number of cookies currently stored. */
size_t wtls_cookie_jar_count(const wtls_cookie_jar *jar);

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
    const char *proxy_url;             /**< optional proxy: "http://[user:pass@]host[:port]" (absolute-form for
                                            http://, CONNECT tunnel for https://; user and password are
                                            percent-decoded and sent as Basic Proxy-Authorization),
                                            "socks5h://[user:pass@]host[:port]" (the proxy resolves host names)
                                            or "socks5://..." (resolved here, IPv4 only; default port 1080).
                                            Bypasses the keep-alive pool. */
    bool no_h2;                        /**< disable HTTP/2 ALPN ("h2") and force HTTP/1.1 */
    wtls_cookie_jar *cookie_jar;       /**< (1.1) optional: Set-Cookie fields of every response, including each
                                            redirect hop, are stored, and matching cookies are sent as Cookie
                                            (RFC 6265: Domain, Path, Secure, Expires/Max-Age; HttpOnly is kept
                                            but means nothing without scripts). A Cookie header you pass in
                                            @c headers wins: then the jar is not consulted for that request.
                                            Domain cookies need a dot in the domain; there is no public suffix
                                            list, so "co.uk" is accepted. Not shared between jars or threads. */
    uint32_t total_timeout_ms;         /**< (1.1) 0 => none. One budget for the whole call: connect, handshake, request,
                                            redirects, retries and every later wtls_http_read() until the body
                                            ends. The per-stage timeouts are cut down to what is left; expiry is
                                            WTLS_ERR_TIMEOUT. Host name resolution is not interruptible. */
    uint8_t max_retries;               /**< (1.1) 0 => none (the one retry on a stale pooled socket stays). Up to this
                                            many more attempts after WTLS_ERR_DNS / _CONNECT / _TIMEOUT / _IO before
                                            the response head arrived, for idempotent methods without body_cb only */
    uint32_t retry_backoff_ms;         /**< (1.1) 0 => 500. The wait before retry n is this << n, +/-25 %, within
                                            total_timeout_ms (WTLS_ERR_TIMEOUT if it no longer fits) */
    bool retry_on_503;                 /**< (1.1) with max_retries: also retry status 429 and 503, waiting the
                                            Retry-After seconds (at most 60) if the server gave them; when the
                                            attempts or the budget run out the last response is returned */
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
    bool br;                /**< (1.1) the body is being decompressed from Brotli (WTLS_BROTLI builds) */
    bool zstd;              /**< (1.1) the body is being decompressed from zstd (WTLS_ZSTD builds) */
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

/**
 * Starts a request without blocking (HTTP/1.1 or HTTP/2 over https://, HTTP/1.1 over http://). Like
 * wtls_http_open(), but the connect, the TLS handshake, the sending of the
 * request and the wait for the response head all happen in later calls of
 * wtls_http_step(). Needs a thread that serves other work meanwhile: the UI
 * thread of an application with a single network thread, or a loop over several
 * requests.
 *
 * On WTLS_WANT_READ / WTLS_WANT_WRITE @p *out is set: wait on
 * wtls_http_get_fd() (or with wtls_http_wait()) and call wtls_http_step()
 * again. Any other return is final: WTLS_OK means the response head is already
 * complete (*out is ready for wtls_http_read()), an error leaves *out NULL. After
 * a later error from wtls_http_step() release the response with
 * wtls_http_close(). Cancel by calling wtls_http_close() at any time: nothing
 * leaks and a half-used connection is never pooled.
 *
 * @p req and everything it points to (URL, headers, body, callbacks, tls) must
 * stay valid until the head is complete or the call failed; body_cb is called
 * from within wtls_http_step() and must not block. Not supported (WTLS_ERR_UNSUPPORTED):
 * proxy_url, expect_100_continue and allow_upgrade. On the Wii the host name lookup and a send larger than IOS's
 * socket buffer space can still block briefly (docs/usage.md). https:// requests negotiate HTTP/2: the connection
 * is shared with other requests started this way to the same host, each on a stream of its own (see
 * docs/usage.md); wtls_http_pool_flush() closes the idle sessions and retires the busy ones.
 * Redirects, cookies, total_timeout_ms, retries (without sleeping: the wait
 * between two attempts is a step that returns WTLS_WANT_READ with a
 * descriptor of -1), gzip/deflate (and br/zstd if built in) and the keep-alive
 * pool work as in wtls_http_open(); a redirect body that has not already
 * arrived is not drained, so that connection is not reused.
 */
wtls_status wtls_http_start(wtls_http_response **out, const wtls_http_request *req);

/**
 * Advances a response started with wtls_http_start().
 *
 * @return WTLS_OK (the head is complete; also on every later call),
 *         WTLS_WANT_READ / WTLS_WANT_WRITE (wait for the descriptor), or the
 *         error that ended the request (sticky; wtls_http_close() the response).
 */
wtls_status wtls_http_step(wtls_http_response *r);

/**
 * The descriptor to wait on for the next step or read (with net_poll() or
 * select()), or -1 when there is nothing to wait on right now: a host
 * name is being resolved or a retry is waiting for its backoff (call
 * wtls_http_step() again after a few milliseconds), or the response is
 * closed. Check wtls_http_pending() first for body bytes.
 */
int wtls_http_get_fd(const wtls_http_response *r);

/** True if wtls_http_read() can deliver bytes (or the end) without touching the socket. */
bool wtls_http_pending(const wtls_http_response *r);

/**
 * Waits until at least one of the @p n responses (NULL entries are skipped, n <= 64)
 * can make progress, at most @p timeout_ms. Responses without a descriptor
 * (name lookup, backoff) count as ready after a short pause. Returns how many
 * can progress (0 = timeout). It is a convenience over net_poll(): the library
 * does not remember which response was ready, so step or read all of them.
 */
int wtls_http_wait(wtls_http_response *const *rs, size_t n, uint32_t timeout_ms);

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
 * On a response from wtls_http_start() it never waits: WTLS_WANT_READ means
 * no byte is available yet (wait on wtls_http_get_fd()); that is not an error
 * and nothing is lost. The I/O timeout then limits the wait without any byte.
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

/**
 * Builds the value of an Authorization header, "Basic <base64(user:pass)>"
 * (RFC 7617), for wtls_http_request::headers. Pass @p out = NULL (with @p out_len)
 * to learn the length without the NUL; otherwise @p out_size must hold it plus one.
 * @p user must not contain ':' and neither string any control character. The
 * client never logs it and strips Authorization when a redirect leaves the origin.
 *
 * @return WTLS_OK, WTLS_ERR_ARG, or WTLS_ERR_HTTP_LIMIT (too long or @p out_size too small).
 */
wtls_status wtls_http_basic_auth(const char *user, const char *pass, char *out, size_t out_size,
                                 size_t *out_len);

/**
 * Builds "Bearer <token>" (RFC 6750); the token must be a b64token (letters,
 * digits and - . _ ~ + /, optionally ending in '='). Buffer rules as for
 * wtls_http_basic_auth().
 */
wtls_status wtls_http_bearer_auth(const char *token, char *out, size_t out_size, size_t *out_len);

/** Options of wtls_http_download(). Zero-initialise; new fields are appended. */
typedef struct {
    const uint8_t *sha256;      /**< optional expected SHA-256 of the whole file (32 bytes) */
    const uint8_t *sig;         /**< optional ECDSA P-256 signature over the file (see <wiitls/sig.h>) */
    size_t sig_len;
    int sig_format;             /**< a wtls_sig_format value (0 = raw r||s, 1 = DER) */
    const uint8_t *sig_pubkey;  /**< required with sig: 65-byte point or SPKI */
    size_t sig_pubkey_len;
    bool no_resume;             /**< discard a leftover <path>.part and start over */
    uint32_t sync_every_bytes;  /**< 0 => 262144: flush + fsync the .part file at this spacing */
    uint64_t max_size;          /**< 0 => unlimited; a larger file is WTLS_ERR_HTTP_LIMIT */
} wtls_http_download_opts;

/** Result of wtls_http_download(). */
typedef struct {
    int status;            /**< last HTTP status */
    uint64_t size;         /**< size of the finished file */
    uint64_t resumed_from; /**< bytes taken over from an earlier attempt (0 = from the start) */
    uint8_t sha256[32];    /**< SHA-256 of the finished file */
} wtls_http_download_result;

/**
 * Downloads req->url to @p path through "<path>.part" (and a small
 * "<path>.part.meta" with the validators), resuming an interrupted attempt with
 * Range / If-Range, and replaces @p path only when the file is complete and
 * verified. Needs wtls_init(); includes the HTTP client's rules otherwise.
 *
 * - Only plain GET requests: method must be NULL or "GET", no body. gzip is
 *   never requested, and req->progress_cb is called with the total progress
 *   including the resumed part.
 * - A resume is only attempted when the server gave a validator (strong ETag
 *   or Last-Modified); if the file changed, the server ignores Range, or the
 *   .part file does not fit, the download restarts from byte 0.
 * - On errors the .part file stays for a later resume, except WTLS_ERR_INTEGRITY
 *   (digest or signature wrong): both temporary files are deleted and @p path
 *   is untouched.
 * - Power loss during the final replacement: call wtls_http_download_recover().
 *   (wtls_http_download() calls it first as well.)
 *
 * @return WTLS_OK, WTLS_ERR_FILE, WTLS_ERR_INTEGRITY, WTLS_ERR_HTTP (status
 *         not 2xx), WTLS_ERR_HTTP_LIMIT, WTLS_ERR_IO (truncated body), WTLS_ERR_ARG,
 *         or any error of wtls_http_open().
 */
wtls_status wtls_http_download(const wtls_http_request *req, const char *path,
                               const wtls_http_download_opts *opt, wtls_http_download_result *out);

/**
 * Repairs an interrupted replacement: if "<path>.old" exists and @p path does
 * not, the old file comes back; if both exist, "<path>.old" is deleted.
 */
wtls_status wtls_http_download_recover(const char *path);

#ifdef __cplusplus
}
#endif

#endif
