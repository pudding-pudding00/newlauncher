/* Download to a file with resume, digest/signature check and an atomic-as-FAT-allows
 * replacement. Built from the public HTTP client (wtls_http_open/read). */
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#define WTLS_FSYNC(f) _commit(_fileno(f))
#else
#include <unistd.h>
#define WTLS_FSYNC(f) fsync(fileno(f))
#endif

#include <wiitls/http.h>
#include <wiitls/sig.h>

#include "wtls_mem.h"

#define NAME_MAX_LEN 300
#define META_MAGIC "WTLSDL1"
#define VALIDATOR_MAX 200
#define IO_BUF 8192u
#define DEFAULT_SYNC 262144u

typedef struct {
    char path[NAME_MAX_LEN], part[NAME_MAX_LEN], meta[NAME_MAX_LEN], old[NAME_MAX_LEN];
} names;

static bool make_names(names *n, const char *path) {
    size_t l = strlen(path);
    if (l == 0 || l + sizeof ".part.meta" > NAME_MAX_LEN) return false;
    memcpy(n->path, path, l + 1);
    snprintf(n->part, sizeof n->part, "%s.part", path);
    snprintf(n->meta, sizeof n->meta, "%s.part.meta", path);
    snprintf(n->old, sizeof n->old, "%s.old", path);
    return true;
}

static bool exists(const char *p) {
    FILE *f = fopen(p, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

/* Size of a file, or -1. */
static long file_size(const char *p) {
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    long sz = -1;
    if (fseek(f, 0, SEEK_END) == 0) sz = ftell(f);
    fclose(f);
    return sz;
}

static void drop_partial(const names *n) {
    remove(n->part);
    remove(n->meta);
}

wtls_status wtls_http_download_recover(const char *path) {
    names n;
    if (!path || !make_names(&n, path)) return WTLS_ERR_ARG;
    if (!exists(n.old)) return WTLS_OK;
    if (exists(n.path)) return remove(n.old) == 0 ? WTLS_OK : WTLS_ERR_FILE; /* the new file is in place */
    return rename(n.old, n.path) == 0 ? WTLS_OK : WTLS_ERR_FILE;           /* put the old one back */
}

/* ---- validators --------------------------------------------------------- */

typedef struct {
    char etag[VALIDATOR_MAX];
    char last_modified[VALIDATOR_MAX];
} validators;

static void take_validators(const wtls_http_response *r, validators *v) {
    const char *e = wtls_http_get_header(r, "etag");
    const char *m = wtls_http_get_header(r, "last-modified");
    snprintf(v->etag, sizeof v->etag, "%s", e && strncmp(e, "W/", 2) != 0 ? e : ""); /* weak ETags cannot validate ranges */
    snprintf(v->last_modified, sizeof v->last_modified, "%s", m ? m : "");
}

static const char *validator_of(const validators *v) {
    if (v->etag[0]) return v->etag;
    if (v->last_modified[0]) return v->last_modified;
    return NULL;
}

static void save_meta(const names *n, const char *url, const validators *v) {
    FILE *f = fopen(n->meta, "wb");
    if (!f) return;
    fprintf(f, "%s\n%s\n%s\n%s\n", META_MAGIC, url, v->etag[0] ? v->etag : "-",
            v->last_modified[0] ? v->last_modified : "-");
    fflush(f);
    WTLS_FSYNC(f);
    fclose(f);
}

static bool line_of(FILE *f, char *out, size_t cap) {
    if (!fgets(out, (int)cap, f)) return false;
    size_t l = strlen(out);
    if (l == 0 || out[l - 1] != '\n') return false; /* longer than the buffer, or cut */
    out[l - 1] = '\0';
    if (l >= 2 && out[l - 2] == '\r') out[l - 2] = '\0';
    return true;
}

static bool load_meta(const names *n, const char *url, validators *v) {
    FILE *f = fopen(n->meta, "rb");
    if (!f) return false;
    char magic[16], u[8300], e[VALIDATOR_MAX], m[VALIDATOR_MAX];
    bool ok = line_of(f, magic, sizeof magic) && strcmp(magic, META_MAGIC) == 0 && line_of(f, u, sizeof u) &&
              strcmp(u, url) == 0 && line_of(f, e, sizeof e) && line_of(f, m, sizeof m);
    fclose(f);
    if (!ok) return false;
    snprintf(v->etag, sizeof v->etag, "%s", strcmp(e, "-") == 0 ? "" : e);
    snprintf(v->last_modified, sizeof v->last_modified, "%s", strcmp(m, "-") == 0 ? "" : m);
    return validator_of(v) != NULL;
}

/* Hashes the first len bytes of the .part file. */
static bool hash_prefix(const char *part, uint64_t len, wtls_sha256_ctx *h, uint8_t *buf) {
    FILE *f = fopen(part, "rb");
    if (!f) return false;
    bool ok = true;
    while (len && ok) {
        size_t want = len < IO_BUF ? (size_t)len : IO_BUF;
        size_t got = fread(buf, 1, want, f);
        if (got == 0) ok = false;
        else if (wtls_sha256_update(h, buf, got) != WTLS_OK) ok = false;
        len -= got;
    }
    fclose(f);
    return ok;
}

/* The total T of a 416 response's unsatisfied-range Content-Range (star, slash, T), or -1. */
static int64_t unsatisfied_total(const wtls_http_response *r) {
    const char *v = wtls_http_get_header(r, "content-range");
    if (!v || strncmp(v, "bytes */", 8) != 0) return -1;
    int64_t t = 0;
    for (const char *p = v + 8; *p; p++) {
        if (*p < '0' || *p > '9' || t > (INT64_MAX - 9) / 10) return -1;
        t = t * 10 + (*p - '0');
    }
    return t;
}

wtls_status wtls_http_download(const wtls_http_request *req, const char *path,
                               const wtls_http_download_opts *opt, wtls_http_download_result *out) {
    static const wtls_http_download_opts k_defaults;
    if (!req || !req->url || !path || !out) return WTLS_ERR_ARG;
    if (!opt) opt = &k_defaults;
    memset(out, 0, sizeof *out);
    if ((req->method && strcmp(req->method, "GET") != 0) || req->body || req->body_cb || req->n_headers > 62 ||
        (opt->sig && (!opt->sig_pubkey || opt->sig_len == 0)))
        return WTLS_ERR_ARG;
    names n;
    if (!make_names(&n, path)) return WTLS_ERR_ARG;
    wtls_status rec = wtls_http_download_recover(path);
    if (rec != WTLS_OK) return rec;

    wtls_status st = WTLS_OK;
    wtls_http_response *resp = NULL;
    FILE *f = NULL;
    uint8_t *buf = wtls_malloc(IO_BUF);
    wtls_http_header *hdrs = wtls_malloc((req->n_headers + 2) * sizeof *hdrs);
    if (!buf || !hdrs) {
        wtls_free(buf);
        wtls_free(hdrs);
        return WTLS_ERR_NOMEM;
    }
    if (req->n_headers) memcpy(hdrs, req->headers, req->n_headers * sizeof *hdrs);

    validators val;
    memset(&val, 0, sizeof val);
    uint64_t offset = 0;
    if (opt->no_resume) {
        drop_partial(&n);
    } else {
        long sz = file_size(n.part);
        if (sz > 0 && load_meta(&n, req->url, &val)) offset = (uint64_t)sz;
        else drop_partial(&n);
    }

    wtls_http_request r = *req;
    r.no_gzip = true;
    r.allow_upgrade = false;
    r.progress_cb = NULL;
    r.progress_ctx = NULL;

    bool complete_already = false; /* 416 for a .part that already has every byte */
    int64_t total = -1;
    uint64_t append_at = 0;
    char range_val[48];
    for (int attempt = 0; attempt < 2; attempt++) {
        r.headers = hdrs;
        r.n_headers = req->n_headers;
        if (offset > 0) {
            if (wtls_http_format_range(offset, 0, false, range_val, sizeof range_val) != WTLS_OK) {
                st = WTLS_ERR_INTERNAL;
                goto done;
            }
            hdrs[r.n_headers++] = (wtls_http_header){"Range", range_val};
            hdrs[r.n_headers++] = (wtls_http_header){"If-Range", validator_of(&val)};
        }
        st = wtls_http_open(&resp, &r);
        if (st != WTLS_OK) goto done; /* .part stays for the next try */
        out->status = wtls_http_status(resp);
        if (offset > 0 && out->status == 416) {
            int64_t t = unsatisfied_total(resp);
            wtls_http_close(resp);
            resp = NULL;
            if (t >= 0 && (uint64_t)t == offset) {
                complete_already = true;
                total = t;
                append_at = offset;
                break;
            }
            offset = 0; /* the .part file is larger than the file now: start over */
            drop_partial(&n);
            continue;
        }
        if (out->status == 206 && offset > 0) {
            int64_t t = -1;
            if (wtls_http_check_resume(resp, offset, &append_at, &t) == WTLS_OK && append_at == offset) {
                total = t;
                break;
            }
            wtls_http_close(resp); /* wrong range: start over */
            resp = NULL;
            offset = 0;
            drop_partial(&n);
            continue;
        }
        if (out->status == 200) {
            total = wtls_http_content_length(resp);
            append_at = 0;
            if (offset > 0) drop_partial(&n); /* the server ignored Range or the file changed */
            break;
        }
        st = WTLS_ERR_HTTP; /* any other status (404, 5xx, 206 without Range, ...) */
        goto done;
    }
    if (!resp && !complete_already) {
        st = WTLS_ERR_HTTP; /* unreachable: the loop ends with a response or a complete file */
        goto done;
    }
    if (opt->max_size && total >= 0 && (uint64_t)total > opt->max_size) {
        st = WTLS_ERR_HTTP_LIMIT;
        drop_partial(&n);
        goto done;
    }

    wtls_sha256_ctx sha;
    st = wtls_sha256_init(&sha);
    if (st != WTLS_OK) goto done;
    if (append_at > 0 && !hash_prefix(n.part, append_at, &sha, buf)) {
        wtls_sha256_abort(&sha);
        drop_partial(&n);
        st = WTLS_ERR_FILE;
        goto done;
    }
    out->resumed_from = append_at;
    uint64_t done_bytes = append_at;

    if (!complete_already) {
        validators fresh;
        take_validators(resp, &fresh);
        if (append_at == 0) val = fresh;
        if (validator_of(&val)) save_meta(&n, req->url, &val);
        f = fopen(n.part, append_at > 0 ? "ab" : "wb");
        if (!f) {
            wtls_sha256_abort(&sha);
            st = WTLS_ERR_FILE;
            goto done;
        }
        uint32_t sync_every = opt->sync_every_bytes ? opt->sync_every_bytes : DEFAULT_SYNC;
        uint64_t since_sync = 0;
        for (;;) {
            int k = wtls_http_read(resp, buf, IO_BUF);
            if (k < 0) {
                st = (wtls_status)k;
                break;
            }
            if (k == 0) break;
            if (fwrite(buf, 1, (size_t)k, f) != (size_t)k) {
                st = WTLS_ERR_FILE;
                break;
            }
            wtls_sha256_update(&sha, buf, (size_t)k);
            done_bytes += (uint64_t)k;
            since_sync += (uint64_t)k;
            if (opt->max_size && done_bytes > opt->max_size) {
                st = WTLS_ERR_HTTP_LIMIT;
                break;
            }
            if (since_sync >= sync_every) {
                fflush(f);
                WTLS_FSYNC(f);
                since_sync = 0;
            }
            if (req->progress_cb) req->progress_cb(req->progress_ctx, (size_t)done_bytes, total);
        }
        if (fflush(f) != 0 && st == WTLS_OK) st = WTLS_ERR_FILE;
        WTLS_FSYNC(f);
        if (fclose(f) != 0 && st == WTLS_OK) st = WTLS_ERR_FILE;
        f = NULL;
        if (st == WTLS_ERR_HTTP_LIMIT) drop_partial(&n);
        if (st == WTLS_OK && total >= 0 && done_bytes != (uint64_t)total) st = WTLS_ERR_IO; /* cut short */
        if (st != WTLS_OK) {
            wtls_sha256_abort(&sha);
            goto done;
        }
    }
    wtls_http_close(resp);
    resp = NULL;

    st = wtls_sha256_finish(&sha, out->sha256);
    if (st != WTLS_OK) goto done;
    if (opt->sha256 && memcmp(opt->sha256, out->sha256, 32) != 0) st = WTLS_ERR_INTEGRITY;
    if (st == WTLS_OK && opt->sig)
        st = wtls_sig_verify_p256_digest(opt->sig_pubkey, opt->sig_pubkey_len, out->sha256, opt->sig, opt->sig_len,
                                         (wtls_sig_format)opt->sig_format);
    if (st != WTLS_OK) {
        if (st == WTLS_ERR_INTEGRITY) drop_partial(&n);
        goto done;
    }

    /* libfat does not replace an existing rename target: move the old file aside first. */
    if (exists(n.path)) {
        remove(n.old);
        if (rename(n.path, n.old) != 0) {
            st = WTLS_ERR_FILE;
            goto done;
        }
    }
    if (rename(n.part, n.path) != 0) {
        st = WTLS_ERR_FILE; /* the next call to ...download_recover() puts the old file back */
        goto done;
    }
    remove(n.old);
    remove(n.meta);
    out->size = done_bytes;
done:
    if (f) fclose(f);
    if (resp) wtls_http_close(resp);
    wtls_free(buf);
    wtls_free(hdrs);
    return st;
}
