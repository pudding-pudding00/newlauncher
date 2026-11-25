/* HTTP/1.1 response heads and message framing (RFC 9112), not installed. */
#ifndef WTLS_HTTP_PARSE_H
#define WTLS_HTTP_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <wiitls/wiitls.h>

#define WTLS_HTTP_MAX_HEAD 16384u /* request and response heads */
#define WTLS_HTTP_MAX_FIELDS 100u

typedef struct {
    const char *name; /* lower case */
    const char *value;
} wtls_http_field;

typedef struct {
    int status; /* 100..599 */
    int minor;  /* 0 or 1 */
    wtls_http_field fields[WTLS_HTTP_MAX_FIELDS];
    size_t n_fields;
} wtls_http_head;

/* Bytes of the head including its empty line; 0 = incomplete. CRLF or bare LF. */
size_t wtls_http_head_end(const char *buf, size_t len);

/* Parses buf[0..head_len) in place: NUL-terminates names and values,
 * lower-cases names, joins obs-fold lines with one SP. buf need not be
 * NUL-terminated. WTLS_OK, WTLS_ERR_HTTP or WTLS_ERR_HTTP_LIMIT (> 100 fields). */
int wtls_http_parse_head(char *buf, size_t head_len, wtls_http_head *out);

/* First field named name (case-insensitive), or NULL. */
const char *wtls_http_head_get(const wtls_http_head *h, const char *name);

typedef enum { WTLS_BODY_NONE, WTLS_BODY_LENGTH, WTLS_BODY_CHUNKED, WTLS_BODY_CLOSE } wtls_body_kind;

typedef struct {
    wtls_body_kind kind;
    uint64_t length; /* WTLS_BODY_LENGTH */
    bool reusable;   /* the connection may carry another request after this body */
} wtls_http_framing;

/* RFC 9112 §6.3 body length. WTLS_OK or WTLS_ERR_HTTP (bad or conflicting
 * Content-Length, Transfer-Encoding other than exactly "chunked"). */
int wtls_http_frame(const wtls_http_head *h, bool head_request, bool keepalive_requested,
                    wtls_http_framing *out);

#endif
