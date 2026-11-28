/* Log callback dispatch. Single-threaded like the rest of the library. */
#include "wtls_log.h"

#include <stdarg.h>
#include <stdio.h>

#define LOG_LINE_MAX 256

static wtls_log_cb s_cb;
static void *s_ctx;
static wtls_log_level s_max_level;
static uint32_t s_categories;

void wtls_set_log_cb(wtls_log_cb cb, void *ctx, wtls_log_level max_level, uint32_t categories) {
    s_cb = cb;
    s_ctx = ctx;
    s_max_level = max_level;
    s_categories = categories;
}

bool wtls_log_enabled(wtls_log_level level, uint32_t category) {
    return s_cb && (int)level <= (int)s_max_level && (category & s_categories) != 0;
}

void wtls_log(wtls_log_level level, uint32_t category, const char *fmt, ...) {
    if (!wtls_log_enabled(level, category)) return;
    char line[LOG_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    s_cb(s_ctx, level, category, line);
}
