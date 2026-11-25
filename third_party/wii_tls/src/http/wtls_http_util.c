/* Two small, stateless HTTP helpers (RFC 3986 percent-encoding for query
 * values, RFC 9110 Content-Range parsing for resumable downloads): no
 * library state, no globals - an application that does not call them links
 * none of this (own object file, pinned by tools/check_no_symbol.cmake). */
#include <stdint.h>
#include <string.h>

#include <wiitls/http.h>

/* RFC 3986 §2.3: ALPHA / DIGIT / "-" / "." / "_" / "~". */
static bool unreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

wtls_status wtls_http_url_encode(const char *in, size_t in_len, char *out, size_t out_size,
                                 size_t *out_len) {
    if (!in || !out_len) return WTLS_ERR_ARG;
    if (in_len > SIZE_MAX - 1u) return WTLS_ERR_HTTP_LIMIT;
    size_t n = 0;
    for (size_t i = 0; i < in_len; i++) {
        size_t add = unreserved((unsigned char)in[i]) ? 1u : 3u;
        if (n > SIZE_MAX - 1u - add) return WTLS_ERR_HTTP_LIMIT;
        n += add;
    }
    if (!out) {
        *out_len = n;
        return WTLS_OK;
    }
    if (out_size < n + 1) return WTLS_ERR_HTTP_LIMIT;
    static const char hex[] = "0123456789ABCDEF";
    size_t k = 0;
    for (size_t i = 0; i < in_len; i++) {
        unsigned char c = (unsigned char)in[i];
        if (unreserved(c)) {
            out[k++] = (char)c;
        } else {
            out[k++] = '%';
            out[k++] = hex[c >> 4];
            out[k++] = hex[c & 15];
        }
    }
    out[n] = '\0';
    *out_len = n;
    return WTLS_OK;
}

/* One decimal number, no sign, no spaces. Advances *pp; false on overflow. */
static bool parse_u64(const char **pp, uint64_t *out) {
    const char *p = *pp;
    if (*p < '0' || *p > '9') return false;
    uint64_t v = 0;
    while (*p >= '0' && *p <= '9') {
        unsigned d = (unsigned)(*p - '0');
        if (v > (UINT64_MAX - d) / 10) return false;
        v = v * 10 + d;
        p++;
    }
    *pp = p;
    *out = v;
    return true;
}

wtls_status wtls_http_parse_content_range(const char *value, wtls_http_content_range *out) {
    if (!value || !out) return WTLS_ERR_ARG;
    const char *p = value;
    while (*p == ' ' || *p == '\t') p++; /* leading OWS */
    if (strncmp(p, "bytes", 5) != 0) return WTLS_ERR_HTTP;
    p += 5;
    const char *after_unit = p;
    while (*p == ' ' || *p == '\t') p++;
    if (p == after_unit) return WTLS_ERR_HTTP; /* RFC 9110 requires the SP */
    if (*p == '*') return WTLS_ERR_HTTP; /* unsatisfied range: a star, no first-last */
    uint64_t start, end;
    if (!parse_u64(&p, &start)) return WTLS_ERR_HTTP;
    if (*p != '-') return WTLS_ERR_HTTP;
    p++;
    if (!parse_u64(&p, &end)) return WTLS_ERR_HTTP;
    if (end < start) return WTLS_ERR_HTTP;
    if (*p != '/') return WTLS_ERR_HTTP;
    p++;
    int64_t total = -1;
    if (*p == '*') {
        p++;
    } else {
        uint64_t t;
        if (!parse_u64(&p, &t)) return WTLS_ERR_HTTP;
        if (t == 0 || end >= t || t > (uint64_t)INT64_MAX) return WTLS_ERR_HTTP;
        total = (int64_t)t;
    }
    while (*p == ' ' || *p == '\t') p++; /* trailing OWS */
    if (*p != '\0') return WTLS_ERR_HTTP;
    out->start = start;
    out->end = end;
    out->total = total;
    return WTLS_OK;
}
