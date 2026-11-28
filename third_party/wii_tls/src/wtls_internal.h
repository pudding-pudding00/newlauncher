/* Internal declarations shared by the library sources (not installed). */
#ifndef WTLS_INTERNAL_H
#define WTLS_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include <mbedtls/ssl.h>
#include <wiitls/wiitls.h>

#define WTLS_PINS_TOTAL (WTLS_SPKI_MAX_PINS + WTLS_SPKI_MAX_PINS_EXT)

/* Validates the pin fields of opt and flattens spki_pins + spki_pins_ext into out;
 * *n = the total. WTLS_OK or WTLS_ERR_ARG. */
int wtls_pins_collect(const wtls_options *opt, uint8_t out[WTLS_PINS_TOTAL][WTLS_SPKI_SHA256_LEN], uint8_t *n);

typedef enum {
    WTLS_CONN_TCP_CONNECT = 0,
    WTLS_CONN_TLS_HANDSHAKE,
    WTLS_CONN_READY,
    WTLS_CONN_FAILED,
    WTLS_CONN_DNS /* host name lookup running on a helper thread */
} wtls_conn_stage;

struct wtls_conn {
    mbedtls_ssl_config conf; /* per connection: ALPN and authmode differ per wtls_options */
    mbedtls_ssl_context ssl;
    int fd;
    wtls_conn_stage stage;
    bool nonblocking;
    size_t write_pending; /* plaintext bytes of an application record that is not completely sent yet */
    uint32_t connect_timeout_ms;
    uint32_t handshake_timeout_ms;
    uint32_t io_timeout_ms;
    uint64_t stage_deadline_ms;
    uint64_t addr_deadline_ms;
    uint64_t connect_t0_us;
    uint64_t connect_w0_us;
    uint64_t handshake_t0_ms;
    uint32_t addrs[4];
    size_t n_addrs;
    size_t addr_idx;
    bool dns_from_cache;
    int dns_job;             /* WTLS_CONN_DNS: the lookup job */
    uint64_t dns_deadline_ms; /* 0: none */
    bool async_dns;          /* resolve on a helper thread */
    bool dns_retried;
    char host[256];          /* normalized: lower case, no trailing dot */
    uint16_t port;
    bool offered_ticket;
    uint32_t lookups_before; /* wtls_castore_lookups before the handshake */
    uint32_t handshake_ms;
    bool ip_literal;
    uint32_t ipv4_be;
    bool no_resumption;
    const wtls_client_cred *cred; /* NULL, or held for the connection's lifetime */
    uint8_t spki_pins[WTLS_PINS_TOTAL][WTLS_SPKI_SHA256_LEN];
    uint8_t n_spki_pins;
    bool pin_matched;
    bool has_id;
    uint8_t id_fp[32];       /* client_cred + spki_pins fingerprint for scache/pool isolation */
    wtls_abort_cb abort_cb;
    void *abort_ctx;
    bool aborted;            /* true once WTLS_ERR_CANCELLED occurred on I/O */
    int bio_err;             /* WTLS_ERR_IO once the transport failed */
    uint8_t *preread;        /* optional early bytes transferred by wtls_http_detach() */
    size_t preread_off;
    size_t preread_len;
};

/* Called first by wtls_shutdown (then reset to NULL). The HTTP layer sets it
 * to wtls_http_pool_flush when it pools a connection, so TLS-only programs do
 * not link the HTTP code. */
extern void (*wtls_shutdown_hook)(void);
/* Optional persistent hook to flush idle HTTP connections on trust-store reset. */
extern void (*wtls_pool_flush_hook)(void);

/* true between a successful wtls_init and wtls_shutdown. */
bool wtls_core_initialized(void);

/* Client config following the Global Constraints (versions, suites, groups, CA
 * callback, verification) plus the per-connection options. 0 or an mbedtls error. */
int wtls_conf_setup(mbedtls_ssl_config *conf, const wtls_options *opt);

/* Multi-IPv4 DNS lookup + TCP connect fallback; commits only the working IP to
 * the DNS cache. Handles net_gethostip()-change resets and abort_cb. */
int wtls_connect_tcp(const char *host, uint16_t port, uint32_t timeout_ms,
                     wtls_abort_cb abort_cb, void *abort_ctx,
                     int *fd_out, uint32_t *ipv4_out, bool *literal_out);

/* Computes the combined client-cert + SPKI-pin identity fingerprint for
 * session-ticket and HTTP keep-alive pool isolation. */
int wtls_conn_identity_fp(const wtls_options *opt, bool *has_id, uint8_t fp[32]);

/* Performs the TLS handshake (and hostname/SPKI verification) over an already
 * connected non-blocking TCP socket @p fd (used for HTTP CONNECT tunnels).
 * Takes ownership of @p fd on both success and failure. */
wtls_status wtls_connect_on_fd(wtls_conn **out, int fd, const char *host, uint16_t port,
                               const wtls_options *opt);

/* Debug single-thread affinity guard (enabled automatically when !NDEBUG or
 * -DWTLS_THREAD_CHECK=ON). Zero cost in Release builds. */
#ifndef WTLS_THREAD_CHECK
#  if !defined(NDEBUG)
#    define WTLS_THREAD_CHECK 1
#  else
#    define WTLS_THREAD_CHECK 0
#  endif
#endif

typedef void (*wtls_thread_violation_fn)(const char *api);
void wtls_set_thread_violation_handler(wtls_thread_violation_fn fn);
bool wtls_thread_guard_check(const char *api);

#if WTLS_THREAD_CHECK
#  define WTLS_THREAD_GUARD(api) wtls_thread_guard_check(api)
#else
#  define WTLS_THREAD_GUARD(api) ((void)(api), true)
#endif

#endif
