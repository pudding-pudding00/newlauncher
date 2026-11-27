/* Stateless HTTP helpers (RFC 3986 percent-encoding, form/multipart body
 * builders, RFC 9110 Range/Content-Range helpers): no library state, no
 * globals - an application that does not call them links none of this
 * (own object file, pinned by tools/check_no_symbol.cmake). */
#include <stdint.h>
#include <stdio.h>
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

wtls_status wtls_http_form_encode(const wtls_http_form_field *fields, size_t n_fields,
                                  char *out, size_t out_size, size_t *out_len) {
    if (!out_len || (n_fields && !fields)) return WTLS_ERR_ARG;
    size_t total = 0;
    for (size_t i = 0; i < n_fields; i++) {
        if (!fields[i].name || !fields[i].value) return WTLS_ERR_ARG;
        size_t kn = 0, vn = 0;
        wtls_status st = wtls_http_url_encode(fields[i].name, strlen(fields[i].name), NULL, 0, &kn);
        if (st != WTLS_OK) return st;
        st = wtls_http_url_encode(fields[i].value, strlen(fields[i].value), NULL, 0, &vn);
        if (st != WTLS_OK) return st;
        size_t add = (i > 0 ? 1u : 0u) + 1u;
        if (total > SIZE_MAX - 1u - add || kn > SIZE_MAX - 1u - total - add ||
            vn > SIZE_MAX - 1u - total - add - kn)
            return WTLS_ERR_HTTP_LIMIT;
        total += add + kn + vn;
    }
    if (!out) {
        *out_len = total;
        return WTLS_OK;
    }
    if (out_size < total + 1u) return WTLS_ERR_HTTP_LIMIT;
    size_t pos = 0;
    for (size_t i = 0; i < n_fields; i++) {
        if (i > 0) out[pos++] = '&';
        size_t n = 0;
        wtls_http_url_encode(fields[i].name, strlen(fields[i].name), out + pos, out_size - pos, &n);
        pos += n;
        out[pos++] = '=';
        wtls_http_url_encode(fields[i].value, strlen(fields[i].value), out + pos, out_size - pos, &n);
        pos += n;
    }
    out[pos] = '\0';
    *out_len = pos;
    return WTLS_OK;
}

static bool safe_param_str(const char *s) {
    if (!s) return true;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c == 0x7f || c == '"') return false;
    }
    return true;
}

enum { MP_PART_HDR = 0, MP_PART_DATA = 1, MP_EPILOGUE = 2, MP_DONE = 3 };

wtls_status wtls_http_multipart_init(wtls_http_multipart *mp,
                                     const wtls_http_multipart_part *parts, size_t n_parts) {
    if (!mp || (n_parts && !parts)) return WTLS_ERR_ARG;
    for (size_t i = 0; i < n_parts; i++) {
        const wtls_http_multipart_part *p = &parts[i];
        if (!p->name || !*p->name || !safe_param_str(p->name) ||
            !safe_param_str(p->filename) || !safe_param_str(p->content_type))
            return WTLS_ERR_ARG;
        if (p->data && p->data_cb) return WTLS_ERR_ARG;
        if (p->data_len && !p->data && !p->data_cb) return WTLS_ERR_ARG;
    }
    memset(mp, 0, sizeof *mp);
    mp->parts = parts;
    mp->n_parts = n_parts;
    static const char k_boundary[] = "----wiitlsFormBoundary7f3a9c2e";
    memcpy(mp->boundary, k_boundary, sizeof k_boundary);
    snprintf(mp->ct_header, sizeof mp->ct_header, "multipart/form-data; boundary=%s", k_boundary);
    mp->phase = n_parts ? MP_PART_HDR : MP_EPILOGUE;
    return WTLS_OK;
}

const char *wtls_http_multipart_content_type(const wtls_http_multipart *mp) {
    return (mp && mp->ct_header[0]) ? mp->ct_header : NULL;
}

static bool format_part_header(wtls_http_multipart *mp) {
    const wtls_http_multipart_part *p = &mp->parts[mp->idx];
    const char *pre = mp->idx == 0 ? "" : "\r\n";
    char boundary[sizeof mp->boundary];
    memcpy(boundary, mp->boundary, sizeof boundary);
    int n;
    if (p->filename) {
        const char *ct = p->content_type ? p->content_type : "application/octet-stream";
        n = snprintf(mp->hdr, sizeof mp->hdr,
                     "%s--%s\r\nContent-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n"
                     "Content-Type: %s\r\n\r\n",
                     pre, boundary, p->name, p->filename, ct);
    } else if (p->content_type) {
        n = snprintf(mp->hdr, sizeof mp->hdr,
                     "%s--%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n"
                     "Content-Type: %s\r\n\r\n",
                     pre, boundary, p->name, p->content_type);
    } else {
        n = snprintf(mp->hdr, sizeof mp->hdr,
                     "%s--%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n",
                     pre, boundary, p->name);
    }
    if (n <= 0 || (size_t)n >= sizeof mp->hdr) return false;
    mp->hdr_len = (size_t)n;
    mp->off = 0;
    return true;
}

int wtls_http_multipart_read(void *ctx, void *buf, size_t len) {
    wtls_http_multipart *mp = ctx;
    if (!mp || (!buf && len)) return -1;
    if (len == 0 || mp->phase == MP_DONE) return 0;
    uint8_t *out = buf;
    size_t written = 0;
    while (written < len && mp->phase != MP_DONE) {
        if (mp->phase == MP_PART_HDR) {
            if (mp->hdr_len == 0 && !format_part_header(mp)) return -1;
            size_t avail = mp->hdr_len - mp->off;
            size_t take = avail < (len - written) ? avail : (len - written);
            memcpy(out + written, mp->hdr + mp->off, take);
            written += take;
            mp->off += take;
            if (mp->off == mp->hdr_len) {
                mp->hdr_len = 0;
                mp->off = 0;
                mp->phase = MP_PART_DATA;
            }
        } else if (mp->phase == MP_PART_DATA) {
            const wtls_http_multipart_part *p = &mp->parts[mp->idx];
            if (p->data_cb) {
                int k = p->data_cb(p->data_ctx, out + written, len - written);
                if (k < 0) return -1;
                if (k > 0) {
                    written += (size_t)k;
                    if (written == len) break;
                    continue;
                }
            } else {
                size_t avail = p->data_len - mp->off;
                size_t take = avail < (len - written) ? avail : (len - written);
                if (take) {
                    memcpy(out + written, (const uint8_t *)p->data + mp->off, take);
                    written += take;
                    mp->off += take;
                }
                if (mp->off < p->data_len) continue;
            }
            mp->off = 0;
            mp->idx++;
            mp->phase = mp->idx < mp->n_parts ? MP_PART_HDR : MP_EPILOGUE;
        } else if (mp->phase == MP_EPILOGUE) {
            if (mp->hdr_len == 0) {
                char boundary[sizeof mp->boundary];
                memcpy(boundary, mp->boundary, sizeof boundary);
                int n = snprintf(mp->hdr, sizeof mp->hdr, "%s--%s--\r\n",
                                 mp->n_parts ? "\r\n" : "", boundary);
                if (n <= 0 || (size_t)n >= sizeof mp->hdr) return -1;
                mp->hdr_len = (size_t)n;
                mp->off = 0;
            }
            size_t avail = mp->hdr_len - mp->off;
            size_t take = avail < (len - written) ? avail : (len - written);
            memcpy(out + written, mp->hdr + mp->off, take);
            written += take;
            mp->off += take;
            if (mp->off == mp->hdr_len) mp->phase = MP_DONE;
        }
    }
    return (int)written;
}

wtls_status wtls_http_format_range(uint64_t start, uint64_t end, bool has_end,
                                   char *out, size_t out_size) {
    if (!out || (has_end && end < start)) return WTLS_ERR_ARG;
    int n = has_end ? snprintf(out, out_size, "bytes=%llu-%llu",
                               (unsigned long long)start, (unsigned long long)end)
                    : snprintf(out, out_size, "bytes=%llu-", (unsigned long long)start);
    if (n < 0 || (size_t)n >= out_size) return WTLS_ERR_HTTP_LIMIT;
    return WTLS_OK;
}

wtls_status wtls_http_check_resume(const wtls_http_response *r, uint64_t resume_from,
                                   uint64_t *append_offset, int64_t *total_len) {
    if (!r || !append_offset || !total_len) return WTLS_ERR_ARG;
    int st = wtls_http_status(r);
    if (st == 206) {
        const char *cr_hdr = wtls_http_get_header(r, "Content-Range");
        if (!cr_hdr) return WTLS_ERR_HTTP;
        wtls_http_content_range cr;
        wtls_status rc = wtls_http_parse_content_range(cr_hdr, &cr);
        if (rc != WTLS_OK || cr.start != resume_from) return WTLS_ERR_HTTP;
        *append_offset = cr.start;
        *total_len = cr.total;
        return WTLS_OK;
    }
    if (st == 200) {
        *append_offset = 0;
        *total_len = wtls_http_content_length(r);
        return WTLS_OK;
    }
    return WTLS_ERR_HTTP;
}
