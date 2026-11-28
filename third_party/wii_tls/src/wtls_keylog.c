/* TLS key log export (NSS SSLKEYLOGFILE format). Compiled in only with
 * WTLS_KEYLOG; release builds contain neither the labels nor the export code. */
#include <string.h>

#include <mbedtls/platform_util.h>

#include "wtls_log.h"

#if defined(WTLS_KEYLOG) && WTLS_KEYLOG

static wtls_keylog_cb s_cb;
static void *s_ctx;

wtls_status wtls_set_keylog_cb(wtls_keylog_cb cb, void *ctx) {
    s_cb = cb;
    s_ctx = ctx;
    return WTLS_OK;
}

static const char *label_for(mbedtls_ssl_key_export_type type) {
    switch (type) {
    case MBEDTLS_SSL_KEY_EXPORT_TLS12_MASTER_SECRET: return "CLIENT_RANDOM";
    case MBEDTLS_SSL_KEY_EXPORT_TLS1_3_CLIENT_EARLY_SECRET: return "CLIENT_EARLY_TRAFFIC_SECRET";
    case MBEDTLS_SSL_KEY_EXPORT_TLS1_3_EARLY_EXPORTER_SECRET: return "EARLY_EXPORTER_SECRET";
    case MBEDTLS_SSL_KEY_EXPORT_TLS1_3_CLIENT_HANDSHAKE_TRAFFIC_SECRET: return "CLIENT_HANDSHAKE_TRAFFIC_SECRET";
    case MBEDTLS_SSL_KEY_EXPORT_TLS1_3_SERVER_HANDSHAKE_TRAFFIC_SECRET: return "SERVER_HANDSHAKE_TRAFFIC_SECRET";
    case MBEDTLS_SSL_KEY_EXPORT_TLS1_3_CLIENT_APPLICATION_TRAFFIC_SECRET: return "CLIENT_TRAFFIC_SECRET_0";
    case MBEDTLS_SSL_KEY_EXPORT_TLS1_3_SERVER_APPLICATION_TRAFFIC_SECRET: return "SERVER_TRAFFIC_SECRET_0";
    default: return NULL;
    }
}

static char *put_hex(char *p, const unsigned char *b, size_t n) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        *p++ = hex[b[i] >> 4];
        *p++ = hex[b[i] & 15];
    }
    return p;
}

static void keylog_export(void *ctx, mbedtls_ssl_key_export_type type, const unsigned char *secret,
                          size_t secret_len, const unsigned char client_random[32],
                          const unsigned char server_random[32], mbedtls_tls_prf_types prf) {
    (void)ctx;
    (void)server_random;
    (void)prf;
    const char *label = label_for(type);
    if (!s_cb || !label || secret_len == 0 || secret_len > 64) return;
    char line[40 + 1 + 64 + 1 + 128 + 1];
    char *p = line;
    size_t ll = strlen(label);
    memcpy(p, label, ll);
    p += ll;
    *p++ = ' ';
    p = put_hex(p, client_random, 32);
    *p++ = ' ';
    p = put_hex(p, secret, secret_len);
    *p = '\0';
    s_cb(s_ctx, line);
    mbedtls_platform_zeroize(line, sizeof line);
}

void wtls_keylog_attach(mbedtls_ssl_context *ssl) {
    if (s_cb) mbedtls_ssl_set_export_keys_cb(ssl, keylog_export, NULL);
}

#else

wtls_status wtls_set_keylog_cb(wtls_keylog_cb cb, void *ctx) {
    (void)cb;
    (void)ctx;
    return WTLS_ERR_UNSUPPORTED;
}

void wtls_keylog_attach(mbedtls_ssl_context *ssl) { (void)ssl; }

#endif
