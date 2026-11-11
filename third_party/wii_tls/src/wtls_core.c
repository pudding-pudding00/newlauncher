#include <stdbool.h>
#include "wtls_internal.h"

const char *wtls_strerror(int status) {
    switch (status) {
    case WTLS_OK:            return "ok";
    case WTLS_WANT_READ:     return "operation would block (read)";
    case WTLS_WANT_WRITE:    return "operation would block (write)";
    case WTLS_ERR_ARG:       return "invalid argument";
    case WTLS_ERR_STATE:     return "invalid state";
    case WTLS_ERR_NOMEM:     return "out of memory";
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
#include "wtls_dns.h"
#include "wtls_scache.h"
#include "wtls_time.h"

static bool s_initialized;

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

wtls_status wtls_init(void) {
    if (s_initialized) return WTLS_OK;
    int r = wtls_plat_init();
    if (r != WTLS_OK) return (wtls_status)r;
    if (psa_crypto_init() != PSA_SUCCESS) return WTLS_ERR_ENTROPY;
    mbedtls_platform_set_time(wtls_mbedtls_time);
    s_initialized = true;
    return WTLS_OK;
}

void wtls_shutdown(void) {
    if (!s_initialized) return;
    wtls_castore_free();
    wtls_scache_clear(); /* holds resumption secrets */
    wtls_dns_clear();
    mbedtls_psa_crypto_free();
    s_initialized = false;
}

wtls_status wtls_add_trusted_ca(const uint8_t *buf, size_t len) {
    if (!s_initialized) return WTLS_ERR_STATE; /* key parsing needs PSA */
    return (wtls_status)wtls_castore_add(buf, len);
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
    return 0;
}
