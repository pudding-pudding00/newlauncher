#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "wtls_internal.h"

const char *wtls_version(void) { return WTLS_VERSION; }

const char *wtls_strerror(int status) {
    switch (status) {
    case WTLS_OK:            return "ok";
    case WTLS_WANT_READ:     return "operation would block (read)";
    case WTLS_WANT_WRITE:    return "operation would block (write)";
    case WTLS_ERR_ARG:       return "invalid argument";
    case WTLS_ERR_STATE:     return "invalid state";
    case WTLS_ERR_NOMEM:     return "out of memory";
    case WTLS_ERR_CANCELLED: return "operation cancelled";
    case WTLS_ERR_UNSUPPORTED: return "feature not compiled in";
    case WTLS_ERR_ENTROPY:   return "entropy source failed";
    case WTLS_ERR_CLOCK:     return "system clock is not plausible (set the Wii clock or sync via SNTP)";
    case WTLS_ERR_NET:       return "network not initialised";
    case WTLS_ERR_DNS:       return "DNS resolution failed";
    case WTLS_ERR_CONNECT:   return "TCP connect failed";
    case WTLS_ERR_TIMEOUT:   return "timeout";
    case WTLS_ERR_IO:        return "connection reset or truncated";
    case WTLS_ERR_CLOSED:    return "connection closed by peer";
    case WTLS_ERR_HANDSHAKE: return "TLS handshake failed";
    case WTLS_ERR_CERT:      return "certificate verification failed";
    case WTLS_ERR_HTTP:      return "malformed HTTP response";
    case WTLS_ERR_HTTP_LIMIT: return "HTTP limit exceeded";
    case WTLS_ERR_HTTP_REDIRECT: return "HTTP redirect refused or too many redirects";
    case WTLS_ERR_WS:        return "WebSocket protocol error";
    case WTLS_ERR_FILE:      return "file I/O error";
    case WTLS_ERR_INTEGRITY: return "integrity check failed (digest or signature)";
    case WTLS_ERR_INTERNAL:  return "internal error";
    default:                 return "unknown error";
    }
}

/* ---- library lifecycle and shared TLS policy ---------------------------- */

#include <mbedtls/platform_time.h>
#include <mbedtls/ssl_ciphersuites.h>
#include <psa/crypto.h>

#include "plat/plat.h"
#include "wtls_castore.h"
#include "wtls_cred.h"
#include "wtls_dns.h"
#include "wtls_entropy.h"
#include "wtls_log.h"
#include "wtls_scache.h"
#include "wtls_time.h"

static bool s_initialized;
static wtls_thread_violation_fn s_thread_violation_fn;

void wtls_set_thread_violation_handler(wtls_thread_violation_fn fn) { s_thread_violation_fn = fn; }

bool wtls_thread_guard_check(const char *api) {
    if (!s_initialized || wtls_plat_thread_ok()) return true;
    wtls_plat_report_thread_violation(api);
    if (s_thread_violation_fn) {
        s_thread_violation_fn(api);
        return false;
    }
    abort();
}

/* ChaCha20-Poly1305 first: no AES hardware on the PPC750. */
static const int k_suites[] = {
    MBEDTLS_TLS1_3_CHACHA20_POLY1305_SHA256,
    MBEDTLS_TLS1_3_AES_128_GCM_SHA256,
    MBEDTLS_TLS1_3_AES_256_GCM_SHA384,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
    MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
    0,
};

/* X25519 first: it is the key share sent in the ClientHello (no HRR round trip). */
static const uint16_t k_groups[] = {
    MBEDTLS_SSL_IANA_TLS_GROUP_X25519,
    MBEDTLS_SSL_IANA_TLS_GROUP_SECP256R1,
    MBEDTLS_SSL_IANA_TLS_GROUP_SECP384R1,
    MBEDTLS_SSL_IANA_TLS_GROUP_NONE,
};

bool wtls_core_initialized(void) { return s_initialized; }

static bool s_init_started; /* wtls_init_start() called, not finished yet */

/* Shared by wtls_init() and wtls_init_step(): psa_crypto_init() takes the
 * entropy from the pool first, the rest inline (same conditioning). */
static wtls_status init_finish(void) {
    int r = wtls_plat_init();
    if (r != WTLS_OK) return (wtls_status)r;
    if (psa_crypto_init() != PSA_SUCCESS) {
        s_init_started = false;
        wtls_entropy_pool_clear();
        return WTLS_ERR_ENTROPY;
    }
    mbedtls_platform_set_time(wtls_mbedtls_time);
    s_initialized = true;
    s_init_started = false;
    wtls_plat_thread_bind();
    return WTLS_OK;
}

wtls_status wtls_init(void) {
    if (s_initialized) {
        if (!WTLS_THREAD_GUARD("wtls_init")) return WTLS_ERR_STATE;
        return WTLS_OK;
    }
    return init_finish();
}

wtls_status wtls_init_start(void) {
    if (s_initialized) {
        if (!WTLS_THREAD_GUARD("wtls_init_start")) return WTLS_ERR_STATE;
        return WTLS_OK;
    }
    s_init_started = true;
    return WTLS_WANT_READ;
}

wtls_status wtls_init_step(uint32_t budget_ms) {
    if (s_initialized) {
        if (!WTLS_THREAD_GUARD("wtls_init_step")) return WTLS_ERR_STATE;
        return WTLS_OK;
    }
    if (!s_init_started) return WTLS_ERR_STATE;
    int r = wtls_entropy_pool_fill(budget_ms);
    if (r == WTLS_WANT_READ) return WTLS_WANT_READ;
    if (r != WTLS_OK) {
        s_init_started = false;
        return WTLS_ERR_ENTROPY;
    }
    return init_finish();
}

void (*wtls_shutdown_hook)(void);
void (*wtls_pool_flush_hook)(void);

void wtls_net_reset(void) {
    if (!WTLS_THREAD_GUARD("wtls_net_reset")) return;
    if (wtls_shutdown_hook) {
        void (*hook)(void) = wtls_shutdown_hook;
        wtls_shutdown_hook = NULL;
        hook();
    }
    wtls_dns_clear();
    WTLS_LOG(WTLS_LOG_INFO, WTLS_LOG_CAT_PLATFORM, "network reset: DNS cache and idle connections dropped");
}

void wtls_shutdown(void) {
    if (!s_initialized) return;
    if (!WTLS_THREAD_GUARD("wtls_shutdown")) return;
    wtls_net_reset();
    wtls_pool_flush_hook = NULL;
    wtls_castore_free();
    wtls_scache_clear(); /* holds resumption secrets */
    mbedtls_psa_crypto_free();
    wtls_entropy_pool_clear();
    wtls_plat_thread_unbind();
    s_initialized = false;
    s_init_started = false;
}

wtls_status wtls_add_trusted_ca(const uint8_t *buf, size_t len) {
    if (!s_initialized) return WTLS_ERR_STATE; /* key parsing needs PSA */
    if (!WTLS_THREAD_GUARD("wtls_add_trusted_ca")) return WTLS_ERR_STATE;
    return (wtls_status)wtls_castore_add(buf, len);
}

void wtls_reset_trusted_cas(bool keep_builtin) {
    if (!s_initialized) return;
    if (!WTLS_THREAD_GUARD("wtls_reset_trusted_cas")) return;
    wtls_castore_reset(keep_builtin);
    wtls_scache_clear();
    if (wtls_pool_flush_hook) wtls_pool_flush_hook();
}

static int try_addrs(const uint32_t *addrs, size_t count, uint16_t port, uint32_t timeout_ms,
                     wtls_abort_cb abort_cb, void *abort_ctx, int *fd_out, uint32_t *ipv4_out) {
    int st = WTLS_ERR_CONNECT;
    uint64_t deadline = wtls_plat_now_ms() + timeout_ms;
    for (size_t i = 0; i < count; i++) {
        uint64_t now = wtls_plat_now_ms();
        if (now >= deadline) return WTLS_ERR_TIMEOUT;
        uint64_t left = deadline - now;
        uint32_t slice = (uint32_t)(left / (count - i));
        if (slice == 0) slice = 1;
        st = wtls_plat_connect_abortable(addrs[i], port, slice, abort_cb, abort_ctx, fd_out);
        if (st == WTLS_OK) {
            *ipv4_out = addrs[i];
            return WTLS_OK;
        }
        if (st == WTLS_ERR_CANCELLED) return WTLS_ERR_CANCELLED;
    }
    return st;
}

int wtls_connect_tcp(const char *host, uint16_t port, uint32_t timeout_ms,
                     wtls_abort_cb abort_cb, void *abort_ctx,
                     int *fd_out, uint32_t *ipv4_out, bool *literal_out) {
    if (wtls_plat_net_changed()) wtls_net_reset();
    uint32_t ip;
    bool literal = wtls_plat_parse_ipv4(host, &ip);
    if (literal_out) *literal_out = literal;
    if (literal) {
        int st = wtls_plat_connect_abortable(ip, port, timeout_ms, abort_cb, abort_ctx, fd_out);
        if (st == WTLS_OK && ipv4_out) *ipv4_out = ip;
        return st;
    }
    uint32_t addrs[WTLS_DNS_MAX_ADDRS];
    size_t count = 0;
    bool from_cache = false;
    if (wtls_dns_lookup_list(host, addrs, &count, &from_cache, wtls_plat_now_ms()) != WTLS_OK)
        return WTLS_ERR_DNS;
    if (abort_cb && abort_cb(abort_ctx)) return WTLS_ERR_CANCELLED;
    uint32_t chosen = 0;
    int st = try_addrs(addrs, count, port, timeout_ms, abort_cb, abort_ctx, fd_out, &chosen);
    if (st == WTLS_OK) {
        wtls_dns_commit(host, chosen, wtls_plat_now_ms());
        if (ipv4_out) *ipv4_out = chosen;
        return WTLS_OK;
    }
    if (st == WTLS_ERR_CANCELLED) return WTLS_ERR_CANCELLED;
    wtls_dns_invalidate(host);
    if (from_cache) {
        if (wtls_dns_lookup_list(host, addrs, &count, &from_cache, wtls_plat_now_ms()) != WTLS_OK)
            return WTLS_ERR_DNS;
        if (abort_cb && abort_cb(abort_ctx)) return WTLS_ERR_CANCELLED;
        st = try_addrs(addrs, count, port, timeout_ms, abort_cb, abort_ctx, fd_out, &chosen);
        if (st == WTLS_OK) {
            wtls_dns_commit(host, chosen, wtls_plat_now_ms());
            if (ipv4_out) *ipv4_out = chosen;
            return WTLS_OK;
        }
    }
    return st;
}

int wtls_pins_collect(const wtls_options *opt, uint8_t out[WTLS_PINS_TOTAL][WTLS_SPKI_SHA256_LEN], uint8_t *n) {
    *n = 0;
    if (!opt) return WTLS_OK;
    if (opt->n_spki_pins > WTLS_SPKI_MAX_PINS || opt->n_spki_pins_ext > WTLS_SPKI_MAX_PINS_EXT ||
        (opt->n_spki_pins_ext && !opt->spki_pins_ext))
        return WTLS_ERR_ARG;
    if (opt->n_spki_pins) memcpy(out, opt->spki_pins, (size_t)opt->n_spki_pins * WTLS_SPKI_SHA256_LEN);
    if (opt->n_spki_pins_ext)
        memcpy(out[opt->n_spki_pins], opt->spki_pins_ext, (size_t)opt->n_spki_pins_ext * WTLS_SPKI_SHA256_LEN);
    *n = (uint8_t)(opt->n_spki_pins + opt->n_spki_pins_ext);
    return WTLS_OK;
}

int wtls_conn_identity_fp(const wtls_options *opt, bool *has_id, uint8_t fp[32]) {
    *has_id = false;
    if (!opt) return WTLS_OK;
    uint8_t pins[WTLS_PINS_TOTAL][WTLS_SPKI_SHA256_LEN];
    uint8_t n_pins = 0;
    if (wtls_pins_collect(opt, pins, &n_pins) != WTLS_OK) return WTLS_ERR_ARG;
    bool use_pins = !opt->insecure_skip_verify && n_pins > 0;
    if (!opt->client_cred && !use_pins) return WTLS_OK;
    if (opt->client_cred && !use_pins) {
        memcpy(fp, opt->client_cred->fp, 32);
        *has_id = true;
        return WTLS_OK;
    }
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    if (psa_hash_setup(&op, PSA_ALG_SHA_256) != PSA_SUCCESS) return WTLS_ERR_INTERNAL;
    uint8_t tag[2] = {opt->client_cred ? 1u : 0u, n_pins};
    psa_status_t pst = psa_hash_update(&op, tag, sizeof tag);
    if (pst == PSA_SUCCESS && opt->client_cred)
        pst = psa_hash_update(&op, opt->client_cred->fp, sizeof opt->client_cred->fp);
    if (pst == PSA_SUCCESS)
        pst = psa_hash_update(&op, (const uint8_t *)pins, (size_t)n_pins * WTLS_SPKI_SHA256_LEN);
    size_t hlen = 0;
    if (pst == PSA_SUCCESS) pst = psa_hash_finish(&op, fp, 32, &hlen);
    if (pst != PSA_SUCCESS || hlen != 32) {
        psa_hash_abort(&op);
        return WTLS_ERR_INTERNAL;
    }
    *has_id = true;
    return WTLS_OK;
}

int wtls_conf_setup(mbedtls_ssl_config *conf, const wtls_options *opt) {
    int r = mbedtls_ssl_config_defaults(conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT);
    if (r != 0) return r;
    mbedtls_ssl_conf_min_tls_version(conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(conf, MBEDTLS_SSL_VERSION_TLS1_3);
    mbedtls_ssl_conf_ciphersuites(conf, k_suites);
    mbedtls_ssl_conf_groups(conf, k_groups);
    mbedtls_ssl_conf_ca_cb(conf, wtls_castore_ca_cb, NULL);
    mbedtls_ssl_conf_authmode(conf, opt->insecure_skip_verify ? MBEDTLS_SSL_VERIFY_NONE
                                                              : MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_session_tickets(conf, opt->no_resumption ? MBEDTLS_SSL_SESSION_TICKETS_DISABLED
                                                              : MBEDTLS_SSL_SESSION_TICKETS_ENABLED);
    if (opt->alpn) {
        r = mbedtls_ssl_conf_alpn_protocols(conf, (const char **)opt->alpn);
        if (r != 0) return r;
    }
#if WTLS_SSL_IN_CONTENT_LEN < 16384
    /* TLS 1.3 gets the record_size_limit extension from the build option; TLS 1.2 has
     * max_fragment_length, which only knows 512, 1024, 2048 and 4096. */
    {
        unsigned char mfl = WTLS_SSL_IN_CONTENT_LEN >= 4096   ? MBEDTLS_SSL_MAX_FRAG_LEN_4096
                            : WTLS_SSL_IN_CONTENT_LEN >= 2048 ? MBEDTLS_SSL_MAX_FRAG_LEN_2048
                            : WTLS_SSL_IN_CONTENT_LEN >= 1024 ? MBEDTLS_SSL_MAX_FRAG_LEN_1024
                                                              : MBEDTLS_SSL_MAX_FRAG_LEN_512;
        r = mbedtls_ssl_conf_max_frag_len(conf, mfl);
        if (r != 0) return r;
    }
#endif
    return 0;
}
