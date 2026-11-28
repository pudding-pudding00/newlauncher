/* Internal logging and key log hooks (not installed). */
#ifndef WTLS_LOG_H
#define WTLS_LOG_H

#include <stdbool.h>
#include <stdint.h>

#include <mbedtls/ssl.h>
#include <wiitls/wiitls.h>

bool wtls_log_enabled(wtls_log_level level, uint32_t category);
void wtls_log(wtls_log_level level, uint32_t category, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/* The arguments are only evaluated and formatted when the message is wanted. */
#define WTLS_LOG(level, category, ...)                       \
    do {                                                     \
        if (wtls_log_enabled((level), (category)))           \
            wtls_log((level), (category), __VA_ARGS__);      \
    } while (0)

/* Registers the key log export on a connection if a callback is installed. */
void wtls_keylog_attach(mbedtls_ssl_context *ssl);

#endif
