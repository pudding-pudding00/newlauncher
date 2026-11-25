/* URL parsing and reference resolution for the HTTP client (not installed). */
#ifndef WTLS_URL_H
#define WTLS_URL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <wiitls/wiitls.h>

#define WTLS_HTTP_MAX_URL 8192u

typedef struct {
    bool secure;             /* https */
    char host[256];          /* lower case, no trailing dot */
    uint16_t port;           /* 443 / 80 unless given */
    const char *target;      /* request-target inside the parsed string: path + query, no fragment */
    size_t target_len;
    bool target_needs_slash; /* the URL has no path: the target is "/" followed by target */
} wtls_url;

/* Strict absolute-URL parser: "http(s)://host[:port][/path][?query][#frag]".
 * Refused (WTLS_ERR_ARG): other schemes, userinfo, IPv6 literals, port
 * 0/>65535/empty/non-digit, host characters outside [A-Za-z0-9.-], bytes
 * outside 0x21..0x7E after the authority, more than WTLS_HTTP_MAX_URL bytes.
 * u->target points into url: valid as long as url is. */
int wtls_url_parse(const char *url, wtls_url *u);

/* RFC 3986 §5.2 reference resolution against an absolute base (scheme in
 * lower case, fragment dropped, dot segments removed). *out is malloc'd.
 * WTLS_OK, WTLS_ERR_ARG (base not absolute) or WTLS_ERR_NOMEM. The result is
 * not validated: parse it with wtls_url_parse(). */
int wtls_url_resolve(const char *base, const char *ref, char **out);

/* Same scheme, host and port. */
bool wtls_url_same_origin(const wtls_url *a, const wtls_url *b);

#endif
