/* Allocation front end for all library memory (not installed). Routes to libc
 * unless the application installed wtls_set_allocator(). Mbed TLS allocations
 * reach the same hooks through mbedtls_platform_set_calloc_free(). */
#ifndef WTLS_MEM_H
#define WTLS_MEM_H

#include <stddef.h>

void *wtls_calloc(size_t n, size_t size);
void *wtls_malloc(size_t size);
void *wtls_realloc(void *p, size_t size); /* p == NULL: malloc; size == 0: frees, returns NULL */
void  wtls_free(void *p);                 /* NULL is allowed */

#endif
