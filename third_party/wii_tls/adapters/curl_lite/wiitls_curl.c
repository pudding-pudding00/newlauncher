/* Minimal libcurl-easy compatibility shim over <wiitls/http.h>. */
#include "curl/curl.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wiitls/http.h>
#include <wiitls/wiitls.h>

struct wtls_curl_handle {
    char *url;
    char *proxy;
    char *custom_method;
    char *user_agent;
    const void *postfields;
    long postfieldsize;
    bool has_postfieldsize;
    bool is_post;
    bool is_nobody;
    bool follow_location;
    long max_redirs;
    bool fail_on_error;
    bool insecure;
    bool no_gzip;
    long connect_timeout_ms;
    long timeout_ms;
    const struct curl_slist *headers;
    curl_write_callback write_cb;
    void *write_ctx;
    curl_write_callback header_cb;
    void *header_ctx;
    long response_code;
    char *userpwd;
    curl_read_callback read_cb;
    void *read_ctx;
    bool upload;
    curl_off_t infilesize; /* -1: unknown */
    bool noprogress;
    curl_xferinfo_callback xfer_cb;
    void *xfer_ctx;
    char *effective_url;
    char *content_type;
    long redirect_count;
    curl_off_t size_download;
};

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1u;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static size_t default_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    FILE *f = userdata ? (FILE *)userdata : stdout;
    return fwrite(ptr, size, nmemb, f);
}

CURLcode curl_global_init(long flags) {
    (void)flags;
    wtls_status st = wtls_init();
    if (st == WTLS_OK) return CURLE_OK;
    return st == WTLS_ERR_NET ? CURLE_COULDNT_CONNECT : CURLE_SSL_CONNECT_ERROR;
}

void curl_global_cleanup(void) { wtls_shutdown(); }

CURL *curl_easy_init(void) {
    struct wtls_curl_handle *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    h->max_redirs = -1; /* libcurl default: 0 follow unless FOLLOWLOCATION */
    h->infilesize = -1;
    h->noprogress = true;
    return h;
}

void curl_easy_cleanup(CURL *curl) {
    struct wtls_curl_handle *h = curl;
    if (!h) return;
    free(h->url);
    free(h->proxy);
    free(h->custom_method);
    free(h->user_agent);
    free(h->userpwd);
    free(h->effective_url);
    free(h->content_type);
    free(h);
}

struct curl_slist *curl_slist_append(struct curl_slist *list, const char *string) {
    if (!string) return list;
    struct curl_slist *node = calloc(1, sizeof *node);
    if (!node) return NULL;
    node->data = dup_str(string);
    if (!node->data) {
        free(node);
        return NULL;
    }
    if (!list) return node;
    struct curl_slist *tail = list;
    while (tail->next) tail = tail->next;
    tail->next = node;
    return list;
}

void curl_slist_free_all(struct curl_slist *list) {
    while (list) {
        struct curl_slist *next = list->next;
        free(list->data);
        free(list);
        list = next;
    }
}

static CURLcode map_status(wtls_status st) {
    switch (st) {
    case WTLS_OK:
        return CURLE_OK;
    case WTLS_ERR_ARG:
        return CURLE_URL_MALFORMAT;
    case WTLS_ERR_NOMEM:
        return CURLE_OUT_OF_MEMORY;
    case WTLS_ERR_DNS:
        return CURLE_COULDNT_RESOLVE_HOST;
    case WTLS_ERR_CONNECT:
        return CURLE_COULDNT_CONNECT;
    case WTLS_ERR_TIMEOUT:
        return CURLE_OPERATION_TIMEDOUT;
    case WTLS_ERR_CERT:
        return CURLE_PEER_FAILED_VERIFICATION;
    case WTLS_ERR_HANDSHAKE:
        return CURLE_SSL_CONNECT_ERROR;
    case WTLS_ERR_CANCELLED:
        return CURLE_ABORTED_BY_CALLBACK;
    case WTLS_ERR_HTTP_REDIRECT:
        return CURLE_TOO_MANY_REDIRECTS;
    default:
        return CURLE_RECV_ERROR;
    }
}

CURLcode curl_easy_setopt(CURL *curl, CURLoption option, ...) {
    struct wtls_curl_handle *h = curl;
    if (!h) return CURLE_BAD_FUNCTION_ARGUMENT;
    va_list ap;
    va_start(ap, option);
    CURLcode rc = CURLE_OK;
    switch (option) {
    case CURLOPT_URL: {
        const char *s = va_arg(ap, const char *);
        free(h->url);
        h->url = dup_str(s);
        if (s && !h->url) rc = CURLE_OUT_OF_MEMORY;
        break;
    }
    case CURLOPT_PROXY: {
        const char *s = va_arg(ap, const char *);
        free(h->proxy);
        h->proxy = dup_str(s);
        if (s && !h->proxy) rc = CURLE_OUT_OF_MEMORY;
        break;
    }
    case CURLOPT_USERAGENT: {
        const char *s = va_arg(ap, const char *);
        free(h->user_agent);
        h->user_agent = dup_str(s);
        if (s && !h->user_agent) rc = CURLE_OUT_OF_MEMORY;
        break;
    }
    case CURLOPT_CUSTOMREQUEST: {
        const char *s = va_arg(ap, const char *);
        free(h->custom_method);
        h->custom_method = dup_str(s);
        if (s && !h->custom_method) rc = CURLE_OUT_OF_MEMORY;
        break;
    }
    case CURLOPT_POSTFIELDS:
        h->postfields = va_arg(ap, const void *);
        h->is_post = true;
        break;
    case CURLOPT_POSTFIELDSIZE:
        h->postfieldsize = va_arg(ap, long);
        h->has_postfieldsize = true;
        break;
    case CURLOPT_POSTFIELDSIZE_LARGE:
        h->postfieldsize = (long)va_arg(ap, curl_off_t);
        h->has_postfieldsize = true;
        break;
    case CURLOPT_USERPWD: {
        const char *s = va_arg(ap, const char *);
        free(h->userpwd);
        h->userpwd = dup_str(s);
        if (s && !h->userpwd) rc = CURLE_OUT_OF_MEMORY;
        break;
    }
    case CURLOPT_READFUNCTION:
        h->read_cb = va_arg(ap, curl_read_callback);
        break;
    case CURLOPT_READDATA:
        h->read_ctx = va_arg(ap, void *);
        break;
    case CURLOPT_UPLOAD:
        h->upload = va_arg(ap, long) != 0;
        break;
    case CURLOPT_INFILESIZE:
        h->infilesize = (curl_off_t)va_arg(ap, long);
        break;
    case CURLOPT_INFILESIZE_LARGE:
        h->infilesize = va_arg(ap, curl_off_t);
        break;
    case CURLOPT_NOPROGRESS:
        h->noprogress = va_arg(ap, long) != 0;
        break;
    case CURLOPT_XFERINFOFUNCTION:
        h->xfer_cb = va_arg(ap, curl_xferinfo_callback);
        break;
    case CURLOPT_XFERINFODATA:
        h->xfer_ctx = va_arg(ap, void *);
        break;
    case CURLOPT_HTTPHEADER:
        h->headers = va_arg(ap, const struct curl_slist *);
        break;
    case CURLOPT_WRITEFUNCTION:
        h->write_cb = va_arg(ap, curl_write_callback);
        break;
    case CURLOPT_WRITEDATA:
        h->write_ctx = va_arg(ap, void *);
        break;
    case CURLOPT_HEADERFUNCTION:
        h->header_cb = va_arg(ap, curl_write_callback);
        break;
    case CURLOPT_HEADERDATA:
        h->header_ctx = va_arg(ap, void *);
        break;
    case CURLOPT_POST:
        h->is_post = va_arg(ap, long) != 0;
        if (h->is_post) h->is_nobody = false;
        break;
    case CURLOPT_HTTPGET:
        if (va_arg(ap, long) != 0) {
            h->is_post = false;
            h->is_nobody = false;
        }
        break;
    case CURLOPT_NOBODY:
        h->is_nobody = va_arg(ap, long) != 0;
        break;
    case CURLOPT_FOLLOWLOCATION:
        h->follow_location = va_arg(ap, long) != 0;
        break;
    case CURLOPT_MAXREDIRS:
        h->max_redirs = va_arg(ap, long);
        break;
    case CURLOPT_FAILONERROR:
        h->fail_on_error = va_arg(ap, long) != 0;
        break;
    case CURLOPT_SSL_VERIFYPEER:
        h->insecure = va_arg(ap, long) == 0;
        break;
    case CURLOPT_SSL_VERIFYHOST:
        (void)va_arg(ap, long);
        break;
    case CURLOPT_CONNECTTIMEOUT_MS:
        h->connect_timeout_ms = va_arg(ap, long);
        break;
    case CURLOPT_TIMEOUT_MS:
        h->timeout_ms = va_arg(ap, long);
        break;
    case CURLOPT_ACCEPT_ENCODING: {
        const char *s = va_arg(ap, const char *);
        h->no_gzip = (!s || !*s);
        break;
    }
    default:
        rc = CURLE_UNKNOWN_OPTION;
        break;
    }
    va_end(ap);
    return rc;
}

typedef struct {
    struct wtls_curl_handle *h;
    curl_off_t ulnow;
    bool aborted;
} upload_ctx;

/* wtls_http_body_cb over CURLOPT_READFUNCTION; reports upload progress. */
static int read_adapter(void *ctx, void *buf, size_t len) {
    upload_ctx *u = ctx;
    size_t n = u->h->read_cb(buf, 1, len, u->h->read_ctx);
    if (n == CURL_READFUNC_ABORT || n > len) {
        u->aborted = true;
        return -1;
    }
    u->ulnow += (curl_off_t)n;
    if (!u->h->noprogress && u->h->xfer_cb && u->h->xfer_cb(u->h->xfer_ctx, 0, 0, u->h->infilesize, u->ulnow)) {
        u->aborted = true;
        return -1;
    }
    return (int)n;
}

CURLcode curl_easy_perform(CURL *curl) {
    struct wtls_curl_handle *h = curl;
    if (!h || !h->url) return CURLE_BAD_FUNCTION_ARGUMENT;
    h->response_code = 0;

    /* Convert slist headers ("Name: Value") + optional User-Agent. */
    wtls_http_header hdrs[65];
    char *hdr_copies[64];
    char auth[2200];
    upload_ctx up = {h, 0, false};
    size_t n_hdr = 0, n_alloc = 0;
    CURLcode rc = CURLE_OK;

    if (h->user_agent) {
        hdrs[n_hdr].name = "User-Agent";
        hdrs[n_hdr].value = h->user_agent;
        n_hdr++;
    }
    if (h->userpwd) {
        const char *colon = strchr(h->userpwd, ':');
        char user[512];
        size_t ul = colon ? (size_t)(colon - h->userpwd) : strlen(h->userpwd);
        if (ul >= sizeof user) return CURLE_BAD_FUNCTION_ARGUMENT;
        memcpy(user, h->userpwd, ul);
        user[ul] = '\0';
        if (wtls_http_basic_auth(user, colon ? colon + 1 : "", auth, sizeof auth, NULL) != WTLS_OK)
            return CURLE_BAD_FUNCTION_ARGUMENT;
        hdrs[n_hdr].name = "Authorization";
        hdrs[n_hdr].value = auth;
        n_hdr++;
    }
    for (const struct curl_slist *it = h->headers; it; it = it->next) {
        if (!it->data) continue;
        const char *colon = strchr(it->data, ':');
        if (!colon) continue;
        if (n_hdr >= 64) {
            rc = CURLE_BAD_FUNCTION_ARGUMENT;
            goto cleanup_hdrs;
        }
        char *cp = dup_str(it->data);
        if (!cp) {
            rc = CURLE_OUT_OF_MEMORY;
            goto cleanup_hdrs;
        }
        hdr_copies[n_alloc++] = cp;
        char *c = cp + (colon - it->data);
        *c = '\0';
        char *val = c + 1;
        while (*val == ' ' || *val == '\t') val++;
        hdrs[n_hdr].name = cp;
        hdrs[n_hdr].value = val;
        n_hdr++;
    }

    wtls_options tls = {0};
    tls.insecure_skip_verify = h->insecure;
    if (h->connect_timeout_ms > 0) tls.connect_timeout_ms = (uint32_t)h->connect_timeout_ms;
    if (h->timeout_ms > 0) tls.io_timeout_ms = (uint32_t)h->timeout_ms;

    wtls_http_request req = {0};
    req.url = h->url;
    req.proxy_url = h->proxy;
    req.headers = n_hdr ? hdrs : NULL;
    req.n_headers = n_hdr;
    req.tls = &tls;
    req.no_gzip = h->no_gzip;
    if (h->timeout_ms > 0) req.header_timeout_ms = (uint32_t)h->timeout_ms;
    if (h->custom_method) req.method = h->custom_method;
    else if (h->is_nobody) req.method = "HEAD";
    else if (h->is_post) req.method = "POST";
    else if (h->upload) req.method = "PUT";
    else req.method = "GET";

    if (h->read_cb && (h->upload || h->is_post)) {
        req.body_cb = read_adapter;
        req.body_ctx = &up;
        curl_off_t size = h->infilesize >= 0 ? h->infilesize : (h->has_postfieldsize ? h->postfieldsize : -1);
        if (size >= 0) req.body_len = (size_t)size;
        else req.body_chunked = true;
    } else if (h->postfields) {
        req.body = h->postfields;
        req.body_len = h->has_postfieldsize && h->postfieldsize >= 0
                           ? (size_t)h->postfieldsize
                           : strlen((const char *)h->postfields);
    }

    if (!h->follow_location) req.max_redirects = -1;
    else if (h->max_redirs > 0) req.max_redirects = (int)h->max_redirs;
    else req.max_redirects = 0;

    wtls_http_response *res = NULL;
    wtls_status st = wtls_http_open(&res, &req);
    if (st != WTLS_OK) {
        rc = up.aborted ? CURLE_ABORTED_BY_CALLBACK : map_status(st);
        goto cleanup_hdrs;
    }

    h->response_code = wtls_http_status(res);
    {
        wtls_http_info info;
        const char *ct = wtls_http_get_header(res, "content-type");
        free(h->effective_url);
        free(h->content_type);
        h->effective_url = NULL;
        h->content_type = dup_str(ct);
        h->redirect_count = 0;
        if (wtls_http_get_info(res, &info) == WTLS_OK) {
            h->effective_url = dup_str(info.url);
            h->redirect_count = info.redirects;
        }
        h->size_download = 0;
    }
    if (h->fail_on_error && h->response_code >= 400) {
        wtls_http_close(res);
        rc = CURLE_HTTP_RETURNED_ERROR;
        goto cleanup_hdrs;
    }

    if (h->header_cb) {
        const char *k, *v;
        char line[512];
        for (size_t i = 0; wtls_http_header_at(res, i, &k, &v); i++) {
            int ln = snprintf(line, sizeof line, "%s: %s\r\n", k, v);
            if (ln > 0 && (size_t)ln < sizeof line) {
                size_t wrote = h->header_cb(line, 1, (size_t)ln, h->header_ctx);
                if (wrote != (size_t)ln) {
                    wtls_http_close(res);
                    rc = CURLE_WRITE_ERROR;
                    goto cleanup_hdrs;
                }
            }
        }
    }

    curl_write_callback wcb = h->write_cb ? h->write_cb : default_write_cb;
    char buf[4096];
    int n;
    while ((n = wtls_http_read(res, buf, sizeof buf)) > 0) {
        size_t wrote = wcb(buf, 1, (size_t)n, h->write_ctx);
        if (wrote != (size_t)n) {
            wtls_http_close(res);
            rc = CURLE_WRITE_ERROR;
            goto cleanup_hdrs;
        }
        h->size_download += n;
        if (!h->noprogress && h->xfer_cb &&
            h->xfer_cb(h->xfer_ctx, wtls_http_content_length(res), h->size_download, h->infilesize, up.ulnow)) {
            wtls_http_close(res);
            rc = CURLE_ABORTED_BY_CALLBACK;
            goto cleanup_hdrs;
        }
    }
    if (n < 0) rc = map_status((wtls_status)n);
    wtls_http_close(res);

cleanup_hdrs:
    for (size_t i = 0; i < n_alloc; i++) free(hdr_copies[i]);
    return rc;
}

CURLcode curl_easy_getinfo(CURL *curl, CURLINFO info, ...) {
    struct wtls_curl_handle *h = curl;
    if (!h) return CURLE_BAD_FUNCTION_ARGUMENT;
    va_list ap;
    va_start(ap, info);
    CURLcode rc = CURLE_OK;
    switch (info) {
    case CURLINFO_RESPONSE_CODE:
    case CURLINFO_REDIRECT_COUNT: {
        long *out = va_arg(ap, long *);
        if (!out) rc = CURLE_BAD_FUNCTION_ARGUMENT;
        else *out = info == CURLINFO_RESPONSE_CODE ? h->response_code : h->redirect_count;
        break;
    }
    case CURLINFO_EFFECTIVE_URL:
    case CURLINFO_CONTENT_TYPE: {
        const char **out = va_arg(ap, const char **);
        if (!out) rc = CURLE_BAD_FUNCTION_ARGUMENT;
        else *out = info == CURLINFO_EFFECTIVE_URL ? h->effective_url : h->content_type; /* valid until the next perform */
        break;
    }
    case CURLINFO_SIZE_DOWNLOAD_T: {
        curl_off_t *out = va_arg(ap, curl_off_t *);
        if (!out) rc = CURLE_BAD_FUNCTION_ARGUMENT;
        else *out = h->size_download;
        break;
    }
    default:
        rc = CURLE_UNKNOWN_OPTION;
        break;
    }
    va_end(ap);
    return rc;
}

const char *curl_easy_strerror(CURLcode code) {
    switch (code) {
    case CURLE_OK: return "No error";
    case CURLE_UNSUPPORTED_PROTOCOL: return "Unsupported protocol";
    case CURLE_URL_MALFORMAT: return "URL using bad/illegal format";
    case CURLE_COULDNT_RESOLVE_HOST: return "Couldn't resolve host name";
    case CURLE_COULDNT_CONNECT: return "Couldn't connect to server";
    case CURLE_HTTP_RETURNED_ERROR: return "HTTP response code said error";
    case CURLE_WRITE_ERROR: return "Failed writing received data";
    case CURLE_OUT_OF_MEMORY: return "Out of memory";
    case CURLE_OPERATION_TIMEDOUT: return "Timeout was reached";
    case CURLE_SSL_CONNECT_ERROR: return "SSL connect error";
    case CURLE_ABORTED_BY_CALLBACK: return "Operation was aborted";
    case CURLE_BAD_FUNCTION_ARGUMENT: return "A libcurl function was given a bad argument";
    case CURLE_TOO_MANY_REDIRECTS: return "Number of redirects hit maximum amount";
    case CURLE_UNKNOWN_OPTION: return "An unknown option was passed";
    case CURLE_READ_ERROR: return "Failed reading the data to upload";
    case CURLE_RECV_ERROR: return "Failure when receiving data from the peer";
    case CURLE_PEER_FAILED_VERIFICATION: return "SSL peer certificate was not OK";
    default: return "Unknown error";
    }
}

/* ---- multi interface -------------------------------------------------------
 * Sequential: curl_multi_perform() runs every added easy handle to completion
 * (one after the other, blocking) on its first call and then reports
 * running_handles = 0. It gives libcurl's API shape, not concurrency. */

struct multi_entry {
    CURL *easy;
    bool done;
    CURLcode result;
    struct multi_entry *next;
};

struct wtls_curl_multi {
    struct multi_entry *entries;
    struct multi_entry *reported; /* cursor for info_read */
    CURLMsg msg;
};

CURLM *curl_multi_init(void) { return calloc(1, sizeof(struct wtls_curl_multi)); }

CURLMcode curl_multi_add_handle(CURLM *multi, CURL *easy) {
    struct wtls_curl_multi *m = multi;
    if (!m) return CURLM_BAD_HANDLE;
    if (!easy) return CURLM_BAD_EASY_HANDLE;
    for (struct multi_entry *e = m->entries; e; e = e->next)
        if (e->easy == easy) return CURLM_BAD_EASY_HANDLE;
    struct multi_entry *e = calloc(1, sizeof *e);
    if (!e) return CURLM_OUT_OF_MEMORY;
    e->easy = easy;
    struct multi_entry **tail = &m->entries;
    while (*tail) tail = &(*tail)->next;
    *tail = e;
    return CURLM_OK;
}

CURLMcode curl_multi_remove_handle(CURLM *multi, CURL *easy) {
    struct wtls_curl_multi *m = multi;
    if (!m) return CURLM_BAD_HANDLE;
    for (struct multi_entry **p = &m->entries; *p; p = &(*p)->next) {
        if ((*p)->easy != easy) continue;
        struct multi_entry *dead = *p;
        *p = dead->next;
        if (m->reported == dead) m->reported = NULL;
        free(dead);
        return CURLM_OK;
    }
    return CURLM_BAD_EASY_HANDLE;
}

CURLMcode curl_multi_perform(CURLM *multi, int *running_handles) {
    struct wtls_curl_multi *m = multi;
    if (!m) return CURLM_BAD_HANDLE;
    for (struct multi_entry *e = m->entries; e; e = e->next) {
        if (e->done) continue;
        e->result = curl_easy_perform(e->easy);
        e->done = true;
    }
    if (running_handles) *running_handles = 0;
    return CURLM_OK;
}

CURLMcode curl_multi_wait(CURLM *multi, struct curl_waitfd *extra_fds, unsigned extra_nfds, int timeout_ms,
                          int *numfds) {
    (void)extra_fds;
    (void)extra_nfds;
    (void)timeout_ms;
    if (!multi) return CURLM_BAD_HANDLE;
    if (numfds) *numfds = 0; /* every transfer already finished inside curl_multi_perform() */
    return CURLM_OK;
}

CURLMsg *curl_multi_info_read(CURLM *multi, int *msgs_in_queue) {
    struct wtls_curl_multi *m = multi;
    if (msgs_in_queue) *msgs_in_queue = 0;
    if (!m) return NULL;
    struct multi_entry *e = m->reported ? m->reported->next : m->entries;
    while (e && !e->done) e = e->next;
    if (!e) return NULL;
    m->reported = e;
    m->msg.msg = CURLMSG_DONE;
    m->msg.easy_handle = e->easy;
    m->msg.data.result = e->result;
    if (msgs_in_queue) {
        int left = 0;
        for (struct multi_entry *r = e->next; r; r = r->next) left += r->done ? 1 : 0;
        *msgs_in_queue = left;
    }
    return &m->msg;
}

CURLMcode curl_multi_cleanup(CURLM *multi) {
    struct wtls_curl_multi *m = multi;
    if (!m) return CURLM_BAD_HANDLE;
    while (m->entries) {
        struct multi_entry *n = m->entries->next;
        free(m->entries);
        m->entries = n;
    }
    free(m);
    return CURLM_OK;
}

const char *curl_multi_strerror(CURLMcode code) {
    switch (code) {
    case CURLM_OK: return "No error";
    case CURLM_BAD_HANDLE: return "Invalid multi handle";
    case CURLM_BAD_EASY_HANDLE: return "Invalid easy handle";
    case CURLM_OUT_OF_MEMORY: return "Out of memory";
    case CURLM_BAD_FUNCTION_ARGUMENT: return "A libcurl function was given a bad argument";
    default: return "Unknown error";
    }
}
