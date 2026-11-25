/* HTTP/1.1 request heads (not installed). */
#ifndef WTLS_HTTP_REQ_H
#define WTLS_HTTP_REQ_H

#include <stdbool.h>
#include <stddef.h>

#include <wiitls/http.h>

#include "http/wtls_http_parse.h"
#include "http/wtls_url.h"

#define WTLS_HTTP_MAX_REQ_HEADERS 64u
#define WTLS_HTTP_DEFAULT_REDIRECTS 5
#define WTLS_HTTP_DEFAULT_HEADER_TIMEOUT_MS 15000u /* request sent -> response head complete */
#define WTLS_HTTP_REDIRECT_DRAIN_MAX 65536u        /* body bytes read to keep a redirect's connection */

/* Builds the request head into a malloc'd, NUL-terminated buffer (*out, *len
 * without the NUL). Order: request line, Host (port only if not the scheme's
 * default), User-Agent and Accept unless the caller set them, Accept-Encoding:
 * gzip if want_gzip and the caller set none, Connection: close if
 * no_keepalive, Transfer-Encoding: chunked if chunked, Content-Length if
 * has_body or the method is POST/PUT/PATCH, then the caller's headers in
 * order. method NULL means GET. WTLS_ERR_ARG: bad method token, bad or
 * reserved header name (Host, Content-Length, Transfer-Encoding, Connection),
 * CR/LF/CTL in a value, more than 64 headers, head over 16384 bytes.
 * WTLS_ERR_NOMEM. */
int wtls_http_build_head(const char *method, const wtls_url *u, const wtls_http_header *h, size_t n,
                         bool has_body, size_t body_len, bool chunked, bool no_keepalive, bool want_gzip,
                         char **out, size_t *len);

/* true if one of h[0..n) is named name (case-insensitive). */
bool wtls_http_headers_have(const wtls_http_header *h, size_t n, const char *name);

/* GET, HEAD, PUT, DELETE, OPTIONS, TRACE (case-sensitive, RFC 9110 §9.2.2). */
bool wtls_http_method_idempotent(const char *method);

/* RFC 9110 token (1*tchar). */
bool wtls_http_is_token(const char *s);

/* ASCII case-insensitive equality. */
bool wtls_http_ieq(const char *a, const char *b);

#endif
