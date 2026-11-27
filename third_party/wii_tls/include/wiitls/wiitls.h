/**
 * @file wiitls.h
 * @brief libwiitls - modern TLS (1.3, 1.2 fallback) client for Wii homebrew.
 *
 * @par Threading
 * libwiitls has no internal locks. Every wtls_* function must be called from
 * one single thread - typically a dedicated network LWP thread of the
 * application, so the UI thread never blocks on the network. The library keeps
 * global state (CA store, session-ticket cache, DNS cache, PSA crypto), which
 * is why the rule covers all functions, not just those on one connection.
 *
 * @par Session tickets
 * TLS 1.3 servers send their session tickets after the handshake. libwiitls
 * captures a ticket only while the application calls wtls_read() or
 * wtls_write() on that connection; a connection that is closed right after
 * wtls_connect() stores no ticket. The next wtls_connect() to the same
 * host:port offers the ticket (single use) and skips certificate-chain work.
 *
 * @par Return values
 * Functions returning ::wtls_status or int report errors as negative
 * ::wtls_status values; wtls_strerror() turns them into text.
 */
#ifndef WIITLS_WIITLS_H
#define WIITLS_WIITLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Library version components; equal the CMake project version (checked at configure time). */
#define WTLS_VERSION_MAJOR 1
#define WTLS_VERSION_MINOR 0
#define WTLS_VERSION_PATCH 0
/** Library version string; equals the CMake project version (checked at configure time). */
#define WTLS_VERSION "1.0.0"

/** Returns the library version string (equals WTLS_VERSION). */
const char *wtls_version(void);

/** Raw SHA-256 SubjectPublicKeyInfo (SPKI) pin length in bytes. */
#define WTLS_SPKI_SHA256_LEN 32u
/** Maximum number of SPKI pins per connection (primary + backup). */
#define WTLS_SPKI_MAX_PINS 2u

/** Status codes. Every error is negative; WTLS_OK is 0. */
typedef enum {
    WTLS_OK            =   0,
    WTLS_WANT_READ     =  -1,  /**< non-blocking mode only: wait until the fd is readable */
    WTLS_WANT_WRITE    =  -2,  /**< non-blocking mode only; pass the same buffer again */
    WTLS_ERR_ARG       = -10,  /**< invalid argument (NULL, port 0, bad host name, bad CA data) */
    WTLS_ERR_STATE     = -11,  /**< wtls_init() has not been called */
    WTLS_ERR_NOMEM     = -12,  /**< out of memory */
    WTLS_ERR_CANCELLED = -13,  /**< operation cancelled by abort_cb */
    WTLS_ERR_ENTROPY   = -20,  /**< the random generator could not be seeded */
    WTLS_ERR_CLOCK     = -21,  /**< system clock implausible; call wtls_time_sync_sntp() */
    WTLS_ERR_NET       = -30,  /**< network not initialised */
    WTLS_ERR_DNS       = -31,  /**< host name could not be resolved */
    WTLS_ERR_CONNECT   = -32,  /**< TCP connect refused or failed */
    WTLS_ERR_TIMEOUT   = -33,  /**< connect, handshake or blocking I/O timed out */
    WTLS_ERR_IO        = -34,  /**< TCP reset or close without close_notify */
    WTLS_ERR_CLOSED    = -35,  /**< clean close_notify from the peer */
    WTLS_ERR_HANDSHAKE = -40,  /**< TLS handshake failed (protocol, version, suite) */
    WTLS_ERR_CERT      = -41,  /**< certificate rejected; see wtls_last_verify_flags() */
    WTLS_ERR_HTTP          = -50,  /**< malformed or unsupported HTTP response (<wiitls/http.h>) */
    WTLS_ERR_HTTP_LIMIT    = -51,  /**< response head, field count or fetch body over its limit */
    WTLS_ERR_HTTP_REDIRECT = -52,  /**< too many redirects, bad Location, or redirect https -> http */
    WTLS_ERR_INTERNAL  = -99,  /**< unexpected library error */
} wtls_status;

/** Opaque connection handle. */
typedef struct wtls_conn wtls_conn;

/** Opaque client certificate + private key pair for mutual TLS (mTLS). */
typedef struct wtls_client_cred wtls_client_cred;

/**
 * Cooperative cancellation callback.
 *
 * Checked before and periodically (at most every 100 ms) during blocking
 * connect, handshake, read, write, and HTTP operations.
 *
 * @return true to abort the current operation with WTLS_ERR_CANCELLED.
 */
typedef bool (*wtls_abort_cb)(void *ctx);

/**
 * Per-connection options. Zero-initialise (`{0}`) and set what you need.
 *
 * @par Source & ABI evolution
 * Always zero-initialise before use; new fields in 1.x minor updates are
 * appended at the end with zero representing the backwards-compatible default.
 */
typedef struct {
    uint32_t connect_timeout_ms;    /**< 0 => 5000  */
    uint32_t handshake_timeout_ms;  /**< 0 => 10000 */
    uint32_t io_timeout_ms;         /**< 0 => 15000, blocking read/write */
    const char *const *alpn;        /**< NULL-terminated list, e.g. {"http/1.1", NULL}; NULL = no ALPN.
                                         Stored by pointer, not copied: the list and its strings
                                         must stay valid until wtls_close() */
    bool no_resumption;             /**< neither use nor store session tickets */
    bool insecure_skip_verify;      /**< TESTS ONLY: disables certificate verification; implies no_resumption */
    const wtls_client_cred *client_cred; /**< NULL => no client certificate; connections retain an internal
                                              reference, so wtls_client_cred_free() may be called while
                                              connections still use it */
    wtls_abort_cb abort_cb;         /**< NULL => no cancellation callback */
    void *abort_ctx;                /**< opaque pointer passed to abort_cb */
    uint8_t spki_pins[WTLS_SPKI_MAX_PINS][WTLS_SPKI_SHA256_LEN]; /**< optional SHA-256 digests of DER
                                                                      SubjectPublicKeyInfo */
    uint8_t n_spki_pins;            /**< 0 => disabled; 1..WTLS_SPKI_MAX_PINS => at least one cert in
                                         the verified chain must match a pin (ignored when
                                         insecure_skip_verify is set; > WTLS_SPKI_MAX_PINS => WTLS_ERR_ARG) */
} wtls_options;

/** Facts about an established connection (see wtls_get_info()). */
typedef struct {
    const char *tls_version;   /**< "TLSv1.3" | "TLSv1.2" */
    const char *ciphersuite;   /**< Mbed TLS suite name */
    const char *alpn;          /**< NULL if not negotiated; points into the caller's opt->alpn strings */
    bool        resumed;       /**< ticket offered AND no chain verification was needed */
    uint32_t    handshake_ms;  /**< TCP connected -> handshake done */
} wtls_conn_info;

/**
 * Initialises the library (platform layer, PSA crypto, entropy).
 *
 * On the Wii, net_init() must have succeeded before. A second call while
 * initialised is a no-op returning WTLS_OK.
 *
 * @return WTLS_OK, WTLS_ERR_NET, or WTLS_ERR_ENTROPY.
 */
wtls_status wtls_init(void);

/**
 * Frees all global state: runtime-added CAs, the session-ticket cache (which
 * holds resumption secrets), the DNS cache and PSA crypto. Close every
 * connection first. Safe to call when not initialised.
 */
void        wtls_shutdown(void);

/**
 * Clears the DNS cache and closes all idle connections in the HTTP
 * keep-alive pool. Call after re-initialising the network (net_deinit /
 * net_init) or changing Wi-Fi access points; on the Wii, a change in
 * net_gethostip() also triggers this automatically.
 */
void        wtls_net_reset(void);

/**
 * Adds a trust anchor in addition to the built-in Mozilla CA bundle (unless
 * built with WTLS_EMBED_CA=OFF or suppressed by wtls_reset_trusted_cas(false)).
 *
 * Accepts DER, or PEM with the terminating NUL byte included in @p len. A PEM
 * with several certificates is added only if all of them parse. The data is
 * copied. Requires wtls_init(); the CA lives until wtls_reset_trusted_cas() or
 * wtls_shutdown().
 *
 * @return WTLS_OK, WTLS_ERR_ARG (unparsable), WTLS_ERR_NOMEM, or WTLS_ERR_STATE.
 */
wtls_status wtls_add_trusted_ca(const uint8_t *buf, size_t len);

/**
 * Resets the runtime trust store: drops all CAs added via
 * wtls_add_trusted_ca(), clears cached session tickets, and flushes any idle
 * pooled HTTP connections.
 *
 * When @p keep_builtin is true (the default after wtls_init()), the built-in
 * CA bundle remains active if compiled in (WTLS_EMBED_CA=ON). When false, the
 * built-in bundle is suppressed so only CAs subsequently added via
 * wtls_add_trusted_ca() (or SPKI pins) are trusted — e.g., to replace the
 * snapshot with a fresh cacert.pem loaded at runtime.
 *
 * Requires wtls_init(). wtls_shutdown() restores the default state.
 */
void        wtls_reset_trusted_cas(bool keep_builtin);

/**
 * Loads a client certificate chain and the matching private key for mutual
 * TLS (set wtls_options::client_cred to present it).
 *
 * @p cert is the certificate chain with the leaf first; @p key is the private
 * key of the leaf. Both accept DER, or PEM with the terminating NUL byte
 * included in len. Supported keys: ECDSA (P-256, P-384) and RSA. For
 * password-protected keys, use wtls_client_cred_load_ex(). The data is copied;
 * it is validated immediately that the private key matches the leaf certificate.
 *
 * The returned credential is reference-counted behind the scenes: connections
 * (including idle keep-alive connections of the HTTP client) hold references,
 * so wtls_client_cred_free() may be called while connections still use it.
 *
 * Requires wtls_init().
 *
 * @return WTLS_OK, WTLS_ERR_ARG (unparsable or cert/key mismatch),
 *         WTLS_ERR_NOMEM, or WTLS_ERR_STATE.
 */
wtls_status wtls_client_cred_load(wtls_client_cred **out,
                                  const uint8_t *cert, size_t cert_len,
                                  const uint8_t *key, size_t key_len);

/**
 * Like wtls_client_cred_load(), additionally accepting an optional password
 * (@p pwd / @p pwd_len) for encrypted PKCS#8 (PBES2) or encrypted PEM keys.
 * Pass @p pwd = NULL and @p pwd_len = 0 for unencrypted keys.
 */
wtls_status wtls_client_cred_load_ex(wtls_client_cred **out,
                                     const uint8_t *cert, size_t cert_len,
                                     const uint8_t *key, size_t key_len,
                                     const uint8_t *pwd, size_t pwd_len);

/** Releases one reference to the credential (zeroizing private key material
 *  once no connection uses it anymore). NULL is safe. */
void wtls_client_cred_free(wtls_client_cred *cred);

/**
 * Opens a TCP connection and completes the TLS handshake (blocking).
 *
 * @p host is a DNS name (case-insensitive, a trailing dot is ignored) or an
 * IPv4 literal; an IP literal sends no SNI and is checked against the
 * certificate's iPAddress SAN. The certificate chain and host name are always
 * verified unless @p opt->insecure_skip_verify is set. TLS 1.3 is preferred,
 * TLS 1.2 is the minimum.
 *
 * @param out   receives the connection, or NULL on any error.
 * @param opt   may be NULL for defaults.
 * @return WTLS_OK; WTLS_ERR_CLOCK when the system clock is implausible (sync it
 *         with wtls_time_sync_sntp() and retry); WTLS_ERR_DNS, WTLS_ERR_CONNECT,
 *         WTLS_ERR_TIMEOUT, WTLS_ERR_CANCELLED, WTLS_ERR_HANDSHAKE, WTLS_ERR_CERT
 *         (details in wtls_last_verify_flags()), WTLS_ERR_STATE, WTLS_ERR_ARG,
 *         WTLS_ERR_NOMEM.
 *
 * WTLS_ERR_CLOCK only catches clocks before the library's build date or more
 * than 20 years after it. A clock that is off by a smaller amount shows up as
 * WTLS_ERR_CERT with MBEDTLS_X509_BADCERT_EXPIRED or _FUTURE: if the clock
 * has not been synced yet, sync it with wtls_time_sync_sntp() and retry once.
 */
wtls_status wtls_connect(wtls_conn **out, const char *host, uint16_t port,
                         const wtls_options *opt);

/**
 * Begins a non-blocking TCP connect + TLS handshake.
 *
 * Resolves @p host (synchronously for DNS if not cached/IPv4 literal), starts
 * the non-blocking TCP connect, and advances as far as possible without
 * blocking.
 *
 * @return WTLS_OK (handshake already finished), WTLS_WANT_READ or
 *         WTLS_WANT_WRITE (*out is set; poll wtls_get_fd(*out) and drive with
 *         wtls_connect_step()), or a negative error (*out is NULL).
 */
wtls_status wtls_connect_start(wtls_conn **out, const char *host, uint16_t port,
                               const wtls_options *opt);

/**
 * Advances an in-progress connection started by wtls_connect_start().
 *
 * On WTLS_WANT_READ or WTLS_WANT_WRITE, wait for socket readiness (on the Wii
 * during TCP connect, IOS net_poll does not wake on connect completion, so
 * poll in <= 5 ms slices and call wtls_connect_step() again) and retry.
 * Once WTLS_OK is returned, the connection is ready for wtls_read()/wtls_write().
 * On any other negative error, the connection is failed and must be freed with
 * wtls_close().
 */
wtls_status wtls_connect_step(wtls_conn *c);

/**
 * Reads decrypted application data.
 *
 * Blocking mode waits up to io_timeout_ms for at least one byte. Also captures
 * session tickets the server sends (see the file comment).
 *
 * @return >0 bytes read, otherwise a ::wtls_status; never 0. WTLS_ERR_CLOSED is
 *         the peer's clean end of stream; WTLS_ERR_IO is a truncated stream
 *         (TCP closed without close_notify). Non-blocking mode returns
 *         WTLS_WANT_READ/WTLS_WANT_WRITE instead of waiting.
 */
int         wtls_read(wtls_conn *c, void *buf, size_t len);

/**
 * Encrypts and sends application data.
 *
 * Blocking mode sends everything unless io_timeout_ms expires or the
 * connection fails part way: then it returns the bytes sent so far (a short
 * count) and the next call reports the error. Non-blocking mode may return
 * fewer bytes; after WTLS_WANT_WRITE/WTLS_WANT_READ call again with the same
 * buffer.
 *
 * @return the number of bytes written (blocking: @p len unless interrupted as
 *         above), or a ::wtls_status when nothing was written.
 */
int         wtls_write(wtls_conn *c, const void *buf, size_t len);

/**
 * Switches wtls_read()/wtls_write() between blocking (default) and
 * non-blocking behaviour. wtls_connect() itself is always blocking.
 *
 * @return WTLS_OK, or WTLS_ERR_ARG for NULL.
 */
wtls_status wtls_set_nonblocking(wtls_conn *c, bool on);

/**
 * Socket descriptor for the application's own net_poll()/select().
 * Check wtls_pending() first: decrypted bytes may be waiting although the
 * socket is not readable. @return the fd, or -1 for NULL.
 */
int         wtls_get_fd(const wtls_conn *c);

/** Already decrypted, unread bytes (0 for NULL). */
size_t      wtls_pending(const wtls_conn *c);

/**
 * Fills @p out with facts about the connection. tls_version and ciphersuite
 * are static strings; alpn points into the caller's opt->alpn list (see
 * wtls_options::alpn). Use them only until wtls_close().
 *
 * @return WTLS_OK, or WTLS_ERR_ARG for NULL.
 */
wtls_status wtls_get_info(const wtls_conn *c, wtls_conn_info *out);

/** MBEDTLS_X509_BADCERT_* flags of the last failed connect (0 if none). */
uint32_t    wtls_last_verify_flags(void);

/** Sends close_notify (best effort) and frees the connection; NULL is allowed. */
void        wtls_close(wtls_conn *c);

/** Human-readable text for a ::wtls_status value (never NULL). */
const char *wtls_strerror(int status);

/**
 * Sets the library's clock from an SNTP server (UDP 123). The Wii's RTC is
 * often wrong, and certificate validity cannot be checked without a clock.
 *
 * @param server     host name or IPv4 literal; NULL = "pool.ntp.org".
 * @param timeout_ms must be > 0.
 * @return WTLS_OK; WTLS_ERR_DNS, WTLS_ERR_TIMEOUT, WTLS_ERR_IO (no or bad
 *         reply), WTLS_ERR_CLOCK (the server's time is implausible),
 *         WTLS_ERR_ENTROPY, WTLS_ERR_ARG.
 */
wtls_status wtls_time_sync_sntp(const char *server, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* WIITLS_WIITLS_H */
