/* Allocator hooks. With a custom allocator every block carries a 16-byte
 * header holding its size, so wtls_realloc() works on allocators without a
 * realloc function and the blocks keep max_align_t alignment. */
#include "wtls_mem.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <mbedtls/platform.h>
#include <wiitls/wiitls.h>

#include "wtls_internal.h"

#define HDR 16u

static wtls_allocator s_alloc;
static bool s_custom;

void *wtls_calloc(size_t n, size_t size) {
    if (!s_custom) return calloc(n, size);
    if (size != 0 && n > (SIZE_MAX - HDR) / size) return NULL;
    size_t total = n * size;
    unsigned char *p = s_alloc.calloc_fn(s_alloc.ctx, 1, total + HDR);
    if (!p) return NULL;
    memcpy(p, &total, sizeof total);
    return p + HDR;
}

void *wtls_malloc(size_t size) { return wtls_calloc(1, size); }

void wtls_free(void *p) {
    if (!p) return;
    if (!s_custom) {
        free(p);
        return;
    }
    s_alloc.free_fn(s_alloc.ctx, (unsigned char *)p - HDR);
}

void *wtls_realloc(void *p, size_t size) {
    if (!s_custom) return realloc(p, size);
    if (!p) return wtls_malloc(size);
    if (size == 0) {
        wtls_free(p);
        return NULL;
    }
    unsigned char *base = (unsigned char *)p - HDR;
    size_t old;
    memcpy(&old, base, sizeof old);
    if (size > SIZE_MAX - HDR) return NULL;
    if (s_alloc.realloc_fn) {
        unsigned char *nb = s_alloc.realloc_fn(s_alloc.ctx, base, size + HDR);
        if (!nb) return NULL;
        memcpy(nb, &size, sizeof size);
        return nb + HDR;
    }
    void *np = wtls_malloc(size);
    if (!np) return NULL;
    memcpy(np, p, old < size ? old : size);
    wtls_free(p);
    return np;
}

static void *mbed_calloc(size_t n, size_t size) { return wtls_calloc(n, size); }
static void mbed_free(void *p) { wtls_free(p); }

wtls_status wtls_set_allocator(const wtls_allocator *a) {
    if (wtls_core_initialized()) return WTLS_ERR_STATE;
    if (a && (!a->calloc_fn || !a->free_fn)) return WTLS_ERR_ARG;
    if (a) {
        s_alloc = *a;
        s_custom = true;
        mbedtls_platform_set_calloc_free(mbed_calloc, mbed_free);
    } else {
        s_custom = false;
        memset(&s_alloc, 0, sizeof s_alloc);
        mbedtls_platform_set_calloc_free(calloc, free);
    }
    return WTLS_OK;
}
