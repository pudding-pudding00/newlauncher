/* Internal declarations shared by the library sources (not installed). */
#ifndef WTLS_INTERNAL_H
#define WTLS_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include <mbedtls/ssl.h>
#include <wiitls/wiitls.h>

struct wtls_conn {
    mbedtls_ssl_config conf; /* per connection: ALPN and authmode differ per wtls_options */
    mbedtls_ssl_context ssl;
    int fd;
    bool nonblocking;
    uint32_t io_timeout_ms;
    char host[256];          /* normalized: lower case, no trailing dot */
    uint16_t port;
    bool offered_ticket;
    uint32_t lookups_before; /* wtls_castore_lookups before the handshake */
    uint32_t handshake_ms;
    bool ip_literal;
    uint32_t ipv4_be;
    bool no_resumption;
    int bio_err;             /* WTLS_ERR_IO once the transport failed */
};

/* true between a successful wtls_init and wtls_shutdown. */
bool wtls_core_initialized(void);

/* Client config following the Global Constraints (versions, suites, groups, CA
 * callback, verification) plus the per-connection options. 0 or an mbedtls error. */
int wtls_conf_setup(mbedtls_ssl_config *conf, const wtls_options *opt);

#endif
