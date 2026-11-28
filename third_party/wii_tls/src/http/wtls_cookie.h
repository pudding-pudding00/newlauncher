/* In-memory cookie jar (RFC 6265, simplified), not installed. No I/O, no
 * clock: callers pass the current Unix time. */
#ifndef WTLS_COOKIE_H
#define WTLS_COOKIE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <wiitls/http.h>

/* Parses one Set-Cookie field value received from host (lower case, no port)
 * for a request to request_path (absolute path, query already removed) and
 * stores, replaces or deletes the cookie. WTLS_OK if the field was understood
 * (also when it deleted a cookie), WTLS_ERR_ARG if it was rejected: no name,
 * too long, a Domain the host does not belong to or that has no dot (no public
 * suffix list: "com" is refused, "co.uk" is not), Secure over plain http. */
int wtls_cookie_jar_set(wtls_cookie_jar *jar, const char *set_cookie, const char *host,
                        const char *request_path, bool secure_conn, int64_t now);

/* "a=1; b=2" for a request to host + path, or NULL if no cookie matches (or out of memory).
 * Longer paths first, then older cookies first. Free with wtls_free (src/wtls_mem.h). */
char *wtls_cookie_jar_header(wtls_cookie_jar *jar, const char *host, const char *request_path,
                             bool secure_conn, int64_t now);

/* "Wed, 21 Oct 2015 07:28:00 GMT" and the other common spellings (RFC 6265 5.1.1): Unix time or -1. */
int64_t wtls_cookie_parse_date(const char *s);

#endif
