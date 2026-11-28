/* wtls_http_start / wtls_http_step / wtls_http_get_fd / wtls_http_wait.
 *
 * Included at the end of wtls_http.c (it works on the same static types). A request in flight is an
 * async_state hanging off the response; wtls_http_step() runs it as far as it can without waiting and
 * says what it waits for. The blocking path is untouched: shared parts are hop_begin(), hop_frame(),
 * follow(), take_live(), cookie_hop_*, apply_deadline() and the body readers (which pass WTLS_WANT_*
 * through on a non-blocking transport). */

typedef enum {
    AS_BEGIN,   /* build the head of the next hop */
    AS_BACKOFF, /* a retry waits for its time */
    AS_ACQUIRE, /* pooled connection or start a new one */
    AS_DNS,     /* plain http: resolving the host name */
    AS_CONNECT, /* plain http: TCP connect */
    AS_TLS,     /* https: wtls_connect_step */
    AS_SEND,    /* request head and body out */
    AS_HEAD,    /* response head in */
    AS_H2WAIT,  /* https: another connection to the host is still negotiating its protocol */
    AS_H2SEND,  /* HTTP/2: stream, request headers and body out */
} as_phase;

struct async_state {
    request_ctx x;
    wtls_url u;
    base_timeouts bt;
    wtls_http_header *filtered;
    cookie_hop ck;
    as_phase phase;
    int failed;      /* sticky final error */
    uint8_t want;    /* what the last WANT_* asked for: 1 read, 2 write */
    unsigned attempt; /* retries used so far */
    uint64_t retry_at_ms;
    bool fresh_only; /* the pooled connection was stale: only a new one will do */
    bool h2_hdr_sent; /* HTTP/2: the request headers are queued on the stream */
    unsigned h2_tries; /* streams repeated on another connection after GOAWAY or REFUSED_STREAM */
    uint64_t h2_wait_since; /* waiting for a busy session's slot (0: not waiting) */
    bool h2_wait_over;      /* waited long enough: a connection of its own now */
    bool got;        /* any response byte arrived on this attempt */
    /* connecting */
    int dns_job;
    uint32_t addrs[WTLS_DNS_MAX_ADDRS];
    size_t n_addrs, addr_idx;
    int fd;
    uint64_t connect_deadline_ms, addr_deadline_ms, dns_deadline_ms;
    wtls_conn *tls;
    /* sending: the current piece is snd[snd_off..snd_len) */
    const uint8_t *snd;
    size_t snd_len, snd_off;
    int snd_stage;    /* 0 head, 1 body from memory, 2 body from the callback, 3 finished */
    uint8_t *cbuf;    /* combined head+body, or the callback's chunk buffer */
    size_t body_left;
    bool cb_last;     /* the piece being sent is the last one of the callback body */
    /* head */
    size_t fill, cap;
    int interim;
    uint64_t head_deadline_ms;
};

#if WTLS_HTTP2
#define H2_SLOT_WAIT_MS 1000u /* how long a request waits for a stream slot of a busy session before it opens a connection */
#endif
#define CB_LEAD 16u /* room before a callback chunk for its "<hex>\r\n" */

static void as_drop_conn_state(wtls_http_response *r, struct async_state *as) {
#if WTLS_HTTP2
    h2_detach(r);
    h2_pend_remove(as);
#endif
    if (as->dns_job >= 0) wtls_dns_job_cancel(as->dns_job);
    as->dns_job = -1;
    if (as->fd >= 0) wtls_plat_close(as->fd);
    as->fd = -1;
    if (as->tls) wtls_close(as->tls);
    as->tls = NULL;
    if (r->conn) wtls_http_tr_close(r->conn);
    r->conn = NULL;
    wtls_free(as->cbuf);
    as->cbuf = NULL;
}

static void async_free(struct async_state *as) {
    if (!as) return;
#if WTLS_HTTP2
    h2_pend_remove(as);
#endif
    if (as->dns_job >= 0) wtls_dns_job_cancel(as->dns_job);
    if (as->fd >= 0) wtls_plat_close(as->fd);
    if (as->tls) wtls_close(as->tls);
    wtls_free(as->cbuf);
    wtls_free((char *)as->x.head);
    wtls_free(as->filtered);
    wtls_free(as->ck.hdrs);
    wtls_free(as->ck.value);
    wtls_free(as);
}

static bool as_can_retry(const struct async_state *as) {
    return as->x.req->max_retries > 0 && !as->x.req->body_cb && wtls_http_method_idempotent(as->x.method);
}

/* Starts the wait for the next attempt (or gives up). */
static int as_schedule_retry(wtls_http_response *r, struct async_state *as, uint32_t wait_ms, int last_error) {
    uint64_t now = wtls_plat_now_ms();
    if (r->deadline_ms && (now >= r->deadline_ms || r->deadline_ms - now <= wait_ms)) return WTLS_ERR_TIMEOUT;
    as_drop_conn_state(r, as);
    wtls_free(r->buf);
    r->buf = NULL;
    memset(&r->head, 0, sizeof r->head);
    wtls_free((char *)as->x.head);
    as->x.head = NULL;
    as->attempt++;
    as->snd_stage = 0;
    as->snd_off = as->snd_len = 0;
    as->retry_at_ms = now + wait_ms;
    as->phase = AS_BACKOFF;
    (void)last_error;
    return WTLS_WANT_READ;
}

#if WTLS_HTTP2
/* The stream cannot go on here (GOAWAY, REFUSED_STREAM, a connection that was gone): the request starts again on
 * another session or connection. */
static int as_h2_retry(wtls_http_response *r, struct async_state *as, int st) {
    if (++as->h2_tries > 3) return st;
    h2_detach(r);
    as->h2_hdr_sent = false;
    as->snd_stage = 0;
    as->snd_off = as->snd_len = 0;
    as->got = false;
    wtls_free(r->buf);
    r->buf = NULL;
    memset(&r->head, 0, sizeof r->head);
    as->phase = AS_ACQUIRE;
    return WTLS_WANT_READ;
}
#endif

static int as_after_error(wtls_http_response *r, struct async_state *as, int st) {
    bool total_over = r->deadline_ms && wtls_plat_now_ms() >= r->deadline_ms;
#if WTLS_HTTP2
    if (r->h2st && !total_over && h2s_stream_retryable(r->h2st) && !as->x.cb_started &&
        wtls_http_method_idempotent(as->x.method))
        return as_h2_retry(r, as, st);
#endif
    if (st == WTLS_ERR_IO && r->conn && r->reused && !as->got && !as->x.cb_started && !as->fresh_only &&
        wtls_http_method_idempotent(as->x.method)) {
        /* the server closed a pooled connection as the request went out: once more on a new one */
        wtls_http_tr_close(r->conn);
        r->conn = NULL;
        wtls_free(as->cbuf);
        as->cbuf = NULL;
        as->fresh_only = true;
        as->snd_stage = 0;
        as->snd_off = as->snd_len = 0;
        as->phase = AS_ACQUIRE;
        return WTLS_WANT_READ;
    }
    if (!total_over && as_can_retry(as) && as->attempt < as->x.req->max_retries &&
        (st == WTLS_ERR_DNS || st == WTLS_ERR_CONNECT || st == WTLS_ERR_TIMEOUT || st == WTLS_ERR_IO)) {
        uint32_t base = as->x.req->retry_backoff_ms ? as->x.req->retry_backoff_ms : 500u;
        int rs = as_schedule_retry(r, as, backoff_for(base, as->attempt), st);
        return rs == WTLS_ERR_TIMEOUT ? WTLS_ERR_TIMEOUT : rs;
    }
    return st;
}

/* ---- connecting ------------------------------------------------------------ */

/* The TCP/TLS transport is up: wrap it, switch it to non-blocking and go on to sending. */
#if WTLS_HTTP2
static int as_h2_attach(wtls_http_response *r, struct async_state *as, h2_sess *hs) {
    r->h2sess = hs;
    hs->refs++;
    hs->idle_since = 0;
    r->reused = hs->used;
    hs->used = true;
    r->info = hs->info;
    r->h2st = NULL;
    as->h2_hdr_sent = false;
    as->snd_stage = 0;
    as->snd_off = as->snd_len = 0;
    as->phase = AS_H2SEND;
    return WTLS_OK;
}
#endif

static int as_wrap_transport(wtls_http_response *r, struct async_state *as, wtls_conn *tls, int fd) {
    http_tr *t = wtls_calloc(1, sizeof *t);
    if (!t) return WTLS_ERR_NOMEM;
#if WTLS_HTTP2
    if (tls && as->x.want_h2) {
        h2_pend_remove(as); /* the protocol is known now */
        wtls_conn_info ci;
        if (wtls_get_info(tls, &ci) == WTLS_OK && ci.alpn && strcmp(ci.alpn, "h2") == 0) {
            t->tls = tls;
            t->fd = -1;
            t->nonblock = true;
            wtls_set_nonblocking(tls, true);
            as->tls = NULL;
            h2_sess *hs = h2_sess_create(r, t);
            if (!hs) return WTLS_ERR_NOMEM;
            return as_h2_attach(r, as, hs);
        }
    }
#endif
    t->tls = tls;
    t->fd = tls ? -1 : fd;
    t->io_timeout_ms = as->x.opt.io_timeout_ms ? as->x.opt.io_timeout_ms : DEF_IO_MS;
    t->abort_cb = as->x.opt.abort_cb;
    t->abort_ctx = as->x.opt.abort_ctx;
    t->nonblock = true;
    if (tls) wtls_set_nonblocking(tls, true);
    r->conn = t;
    as->tls = NULL;
    as->fd = -1;
    as->phase = AS_SEND;
    as->snd_stage = 0;
    as->snd_off = as->snd_len = 0;
    return WTLS_OK;
}

static int as_plain_connect(wtls_http_response *r, struct async_state *as) {
    for (;;) {
        uint64_t now = wtls_plat_now_ms();
        if (as->fd < 0) {
            if (as->addr_idx >= as->n_addrs) return WTLS_ERR_CONNECT;
            if (now >= as->connect_deadline_ms) return WTLS_ERR_TIMEOUT;
            uint64_t left = as->connect_deadline_ms - now;
            uint64_t slice = left / (as->n_addrs - as->addr_idx);
            as->addr_deadline_ms = now + (slice ? slice : 1);
            int st = wtls_plat_connect_begin(as->addrs[as->addr_idx], as->u.port, &as->fd);
            if (st == WTLS_OK) goto connected;
            if (st == WTLS_WANT_WRITE) {
                as->want = 2;
                return WTLS_WANT_WRITE;
            }
            as->fd = -1;
            as->addr_idx++;
            continue;
        }
        int st = wtls_plat_connect_check(as->fd, as->addrs[as->addr_idx], as->u.port);
        if (st == WTLS_OK) goto connected;
        if (st == WTLS_WANT_WRITE) {
            if (now < as->addr_deadline_ms) {
                as->want = 2;
                return WTLS_WANT_WRITE;
            }
            if (now >= as->connect_deadline_ms) return WTLS_ERR_TIMEOUT;
        }
        wtls_plat_close(as->fd); /* refused, or this address ran out of its share of the time */
        as->fd = -1;
        as->addr_idx++;
    }
connected:
    wtls_dns_commit(as->u.host, as->addrs[as->addr_idx], wtls_plat_now_ms());
    return as_wrap_transport(r, as, NULL, as->fd);
}

static int as_acquire(wtls_http_response *r, struct async_state *as) {
    request_ctx *x = &as->x;
#if WTLS_HTTP2
    const bool h2_able = as->u.secure && x->want_h2;
    if (h2_able) {
        h2_sess_gc(wtls_plat_now_ms());
        h2_sess *hs = h2_sess_find(r);
        if (hs) return as_h2_attach(r, as, hs);
    }
#endif
    if (!as->fresh_only && take_live(r, x, false)) {
        http_tr *t = r->conn;
        t->nonblock = true;
        if (t->tls) wtls_set_nonblocking(t->tls, true);
        as->phase = AS_SEND;
        as->snd_stage = 0;
        as->snd_off = as->snd_len = 0;
        return WTLS_OK;
    }
    r->reused = false;
#if WTLS_HTTP2
    if (h2_able && (h2_pend_find(r) || (!as->h2_wait_over && h2_full_find(r)))) {
        /* someone is finding out whether the host speaks HTTP/2, or its session has no free stream yet */
        as->phase = AS_H2WAIT;
        as->want = 1;
        as->h2_wait_since = wtls_plat_now_ms();
        return WTLS_WANT_READ;
    }
#endif
    if (as->u.secure) {
        wtls_options opt = x->opt;
        opt.async_dns = true;
        int st = wtls_connect_start(&as->tls, as->u.host, as->u.port, &opt);
        if (st == WTLS_OK) return as_wrap_transport(r, as, as->tls, -1);
        if (st == WTLS_WANT_READ || st == WTLS_WANT_WRITE) {
#if WTLS_HTTP2
            if (h2_able && h2_pend_add(r, as) != WTLS_OK) {
                wtls_close(as->tls);
                as->tls = NULL;
                return WTLS_ERR_NOMEM;
            }
#endif
            as->phase = AS_TLS;
            as->want = st == WTLS_WANT_READ ? 1 : 2;
        }
        return st;
    }
    bool cached = false;
    int st = wtls_dns_lookup_begin(as->u.host, &as->dns_job, as->addrs, &as->n_addrs, &cached, wtls_plat_now_ms());
    if (st == WTLS_WANT_READ) {
        as->phase = AS_DNS;
        as->want = 1;
        as->dns_deadline_ms = x->opt.dns_timeout_ms ? wtls_plat_now_ms() + x->opt.dns_timeout_ms : 0;
        return WTLS_WANT_READ;
    }
    if (st != WTLS_OK) return st == WTLS_ERR_NOMEM ? WTLS_ERR_NOMEM : WTLS_ERR_DNS;
    as->dns_job = -1;
    as->addr_idx = 0;
    as->phase = AS_CONNECT;
    as->connect_deadline_ms = wtls_plat_now_ms() + (x->opt.connect_timeout_ms ? x->opt.connect_timeout_ms : DEF_CONNECT_MS);
    return as_plain_connect(r, as);
}

/* ---- sending --------------------------------------------------------------- */

static int as_begin_send(wtls_http_response *r, struct async_state *as) {
    request_ctx *x = &as->x;
    if (r->conn->tls) wtls_get_info(r->conn->tls, &r->info);
    else memset(&r->info, 0, sizeof r->info);
    size_t body_len = x->body ? x->body_len : 0;
    if (body_len && x->head_len + body_len <= SINGLE_WRITE_MAX) {
        as->cbuf = wtls_malloc(x->head_len + body_len);
        if (!as->cbuf) return WTLS_ERR_NOMEM;
        memcpy(as->cbuf, x->head, x->head_len);
        memcpy(as->cbuf + x->head_len, x->body, body_len);
        as->snd = as->cbuf;
        as->snd_len = x->head_len + body_len;
        as->snd_stage = 3; /* nothing follows */
    } else {
        as->snd = (const uint8_t *)x->head;
        as->snd_len = x->head_len;
        as->snd_stage = 0;
    }
    as->snd_off = 0;
    as->body_left = x->body_len;
    return WTLS_OK;
}

/* Produces the next piece of a callback body in cbuf (see send_body_cb for the rules). */
static int as_next_cb_piece(wtls_http_response *r, struct async_state *as) {
    request_ctx *x = &as->x;
    (void)r;
    if (!as->cbuf) {
        as->cbuf = wtls_malloc(UPLOAD_BUF_SIZE + CB_LEAD + 8);
        if (!as->cbuf) return WTLS_ERR_NOMEM;
    }
    size_t want = UPLOAD_BUF_SIZE;
    if (!x->body_chunked && want > as->body_left) want = as->body_left;
    x->cb_started = true;
    int k = x->body_cb(x->body_ctx, as->cbuf + CB_LEAD, want);
    if (k < 0 || (size_t)k > want) return WTLS_ERR_IO;
    if (!x->body_chunked) {
        if (k == 0) return WTLS_ERR_IO; /* short of body_len: no rewind */
        as->snd = as->cbuf + CB_LEAD;
        as->snd_len = (size_t)k;
        as->body_left -= (size_t)k;
        as->snd_stage = as->body_left ? 2 : 3;
    } else if (k == 0) {
        as->snd = (const uint8_t *)"0\r\n\r\n";
        as->snd_len = 5;
        as->snd_stage = 3;
    } else {
        char hex[12];
        int n = snprintf(hex, sizeof hex, "%x\r\n", (unsigned)k);
        memcpy(as->cbuf + CB_LEAD - (size_t)n, hex, (size_t)n);
        as->cbuf[CB_LEAD + (size_t)k] = '\r';
        as->cbuf[CB_LEAD + (size_t)k + 1] = '\n';
        as->snd = as->cbuf + CB_LEAD - (size_t)n;
        as->snd_len = (size_t)n + (size_t)k + 2;
        as->snd_stage = 2;
    }
    as->snd_off = 0;
    return WTLS_OK;
}

static int as_enter_head_phase(wtls_http_response *r, struct async_state *as) {
    wtls_free(as->cbuf);
    as->cbuf = NULL;
    wtls_free(r->buf);
    r->buf = wtls_malloc(HEAD_BUF_START);
    if (!r->buf) return WTLS_ERR_NOMEM;
    as->cap = HEAD_BUF_START;
    as->fill = 0;
    as->interim = 0;
    memset(&r->head, 0, sizeof r->head);
    as->head_deadline_ms = wtls_plat_now_ms() + as->x.header_timeout;
    as->phase = AS_HEAD;
    return WTLS_OK;
}

static int as_send(wtls_http_response *r, struct async_state *as) {
    request_ctx *x = &as->x;
    for (;;) {
        while (as->snd_off < as->snd_len) {
            int k = r->conn->tls ? wtls_write(r->conn->tls, as->snd + as->snd_off, as->snd_len - as->snd_off)
                                 : plain_write(r->conn, as->snd + as->snd_off, as->snd_len - as->snd_off);
            if (k == WTLS_WANT_READ || k == WTLS_WANT_WRITE) {
                as->want = k == WTLS_WANT_READ ? 1 : 2;
                return k;
            }
            if (k <= 0) return k == WTLS_ERR_CLOSED || k == 0 ? WTLS_ERR_IO : k;
            as->snd_off += (size_t)k;
        }
        if (as->snd_stage == 0) { /* the head is out: the body, if any */
            if (x->body_cb) {
                if (!x->body_chunked && x->body_len == 0) return as_enter_head_phase(r, as);
                as->snd_stage = 2;
            } else if (x->body && x->body_len) {
                as->snd = x->body;
                as->snd_len = x->body_len;
                as->snd_off = 0;
                as->snd_stage = 3;
                continue;
            } else {
                return as_enter_head_phase(r, as);
            }
        }
        if (as->snd_stage == 3) return as_enter_head_phase(r, as);
        int st = as_next_cb_piece(r, as);
        if (st != WTLS_OK) return st;
    }
}


/* ---- HTTP/2: one stream of a shared session ---------------------------------------------------------- */

#if WTLS_HTTP2
static void as_h2_want(struct async_state *as, const h2_sess *hs) {
    as->want = (uint8_t)(1u | (h2s_wants(hs->s) & 2u)); /* the write side only while output is stuck */
}

static int as_h2_enter_head(wtls_http_response *r, struct async_state *as) {
    wtls_free(as->cbuf);
    as->cbuf = NULL;
    wtls_free(r->buf);
    r->buf = NULL;
    memset(&r->head, 0, sizeof r->head);
    as->head_deadline_ms = wtls_plat_now_ms() + as->x.header_timeout;
    as->phase = AS_HEAD;
    return WTLS_OK;
}

static int as_h2_send(wtls_http_response *r, struct async_state *as) {
    request_ctx *x = &as->x;
    h2_sess *hs = r->h2sess;
    h2s_pump(hs->s);
    if (!r->h2st) {
        int st = h2s_stream_open(hs->s, &r->h2st);
        if (st == WTLS_WANT_READ) { /* every slot is busy: wait for one */
            as_h2_want(as, hs);
            return st;
        }
        if (st == WTLS_ERR_IO) return as_h2_retry(r, as, st); /* the session takes no more streams */
        if (st != WTLS_OK) return st;
    }
    if (!as->h2_hdr_sent) {
        bool has_body = x->body != NULL || x->body_cb != NULL;
        bool has_cl = !x->body_chunked && (has_body || strcmp(x->method, "POST") == 0 ||
                                           strcmp(x->method, "PUT") == 0 || strcmp(x->method, "PATCH") == 0);
        int st = h2s_stream_send_headers(r->h2st, x->method, x->url, x->headers, x->n_headers, has_cl,
                                         has_body ? x->body_len : 0, has_body, x->want_gzip);
        if (st == WTLS_WANT_READ) { /* the server's concurrent stream limit */
            as_h2_want(as, hs);
            return st;
        }
        if (st == WTLS_ERR_IO) return as_h2_retry(r, as, st);
        if (st != WTLS_OK) return st;
        as->h2_hdr_sent = true;
        as->snd_stage = x->body_cb ? 2 : (x->body ? 1 : 3);
        as->snd_off = as->snd_len = 0;
        as->body_left = x->body_len;
    }
    for (;;) {
        h2s_stream *st = r->h2st;
        if (as->snd_stage == 3 || h2s_stream_request_done(st)) {
            h2s_pump(hs->s);
            return as_h2_enter_head(r, as);
        }
        int n;
        if (as->snd_stage == 1) {
            n = h2s_stream_send_data(st, (const uint8_t *)x->body + as->snd_off, x->body_len - as->snd_off, true);
            if (n > 0) as->snd_off += (size_t)n;
        } else {
            if (as->snd_off >= as->snd_len) { /* the next piece from the callback */
                if (!as->cbuf) {
                    as->cbuf = wtls_malloc(UPLOAD_BUF_SIZE);
                    if (!as->cbuf) return WTLS_ERR_NOMEM;
                }
                size_t want = UPLOAD_BUF_SIZE;
                if (!x->body_chunked && want > as->body_left) want = as->body_left;
                int k = 0;
                if (want) {
                    x->cb_started = true;
                    k = x->body_cb(x->body_ctx, as->cbuf, want);
                    if (k < 0 || (size_t)k > want) return WTLS_ERR_IO;
                }
                if (!x->body_chunked) {
                    if (want && k == 0) return WTLS_ERR_IO; /* short of body_len: no rewind */
                    as->body_left -= (size_t)k;
                    as->cb_last = as->body_left == 0;
                } else {
                    as->cb_last = k == 0;
                }
                as->snd = as->cbuf;
                as->snd_len = (size_t)k;
                as->snd_off = 0;
            }
            n = h2s_stream_send_data(st, as->snd + as->snd_off, as->snd_len - as->snd_off, as->cb_last);
            if (n > 0) as->snd_off += (size_t)n;
        }
        if (n < 0) return n;
        h2s_pump(hs->s);
        if (n == 0 && !h2s_stream_request_done(st)) { /* flow control or a full output queue */
            as_h2_want(as, hs);
            return WTLS_WANT_READ;
        }
    }
}

static int as_h2_read_head(wtls_http_response *r, struct async_state *as) {
    h2_sess *hs = r->h2sess;
    h2s_pump(hs->s);
    char *arena = NULL;
    int st = h2s_stream_head(r->h2st, &r->head, &arena);
    if (st == WTLS_OK) {
        r->buf = arena;
        as->got = true;
        return WTLS_OK;
    }
    if (st == WTLS_WANT_READ) {
        if (wtls_plat_now_ms() >= as->head_deadline_ms) return WTLS_ERR_TIMEOUT;
        as_h2_want(as, hs);
        return st;
    }
    as->got = h2s_stream_got_data(r->h2st);
    return st;
}
#endif

/* ---- the response head ------------------------------------------------------- */

static int as_read_head(wtls_http_response *r, struct async_state *as) {
    for (;;) {
        size_t end = wtls_http_head_end(r->buf, as->fill);
        if (end) {
            int st = wtls_http_parse_head(r->buf, end, &r->head);
            if (st != WTLS_OK) return st;
            if (r->head.status >= 200) {
                r->extra_off = end;
                r->extra_end = as->fill;
                return WTLS_OK;
            }
            if (r->head.status == 101) return WTLS_ERR_HTTP; /* no upgrade was asked for */
            if (++as->interim > 16) return WTLS_ERR_HTTP_LIMIT;
            memmove(r->buf, r->buf + end, as->fill - end); /* interim 1xx: skip it */
            as->fill -= end;
            memset(&r->head, 0, sizeof r->head);
            continue;
        }
        if (as->fill == WTLS_HTTP_MAX_HEAD) return WTLS_ERR_HTTP_LIMIT;
        if (as->fill == as->cap) {
            size_t ncap = as->cap * 2 > WTLS_HTTP_MAX_HEAD ? WTLS_HTTP_MAX_HEAD : as->cap * 2;
            char *nb = wtls_realloc(r->buf, ncap);
            if (!nb) return WTLS_ERR_NOMEM;
            r->buf = nb;
            as->cap = ncap;
        }
        if (wtls_plat_now_ms() >= as->head_deadline_ms) return WTLS_ERR_TIMEOUT;
        int k = tr_read(r, r->buf + as->fill, as->cap - as->fill);
        if (k == WTLS_WANT_READ || k == WTLS_WANT_WRITE) {
            as->want = k == WTLS_WANT_READ ? 1 : 2;
            return k;
        }
        if (k > 0) {
            as->fill += (size_t)k;
            as->got = true;
            continue;
        }
        return k == WTLS_ERR_CLOSED ? WTLS_ERR_IO : k; /* closed before a complete head */
    }
}

/* ---- the machine ----------------------------------------------------------------- */

static int as_run(wtls_http_response *r, struct async_state *as);

/* The final head of this hop is in: framing, cookies, then a redirect, a 429/503 retry, or done. */
static int as_head_complete(wtls_http_response *r, struct async_state *as) {
    request_ctx *x = &as->x;
    const wtls_http_request *req = x->req;
    wtls_free((char *)x->head);
    x->head = NULL;
    cookie_hop_end(x, &as->ck); /* the headers went out (and may be needed again for a redirect) */
    int st = hop_frame(r, x);
    if (st != WTLS_OK) return st;
    cookie_hop_store(req, r, &as->u, &as->ck);
    int code = r->head.status;
    if (req->retry_on_503 && (code == 429 || code == 503) && as_can_retry(as) && as->attempt < req->max_retries) {
        const char *ra = wtls_http_head_get(&r->head, "retry-after");
        uint32_t secs = 0;
        bool digits = ra && *ra;
        for (const char *c = ra; digits && *c; c++) {
            if (*c < '0' || *c > '9' || secs > 60) digits = false;
            else secs = secs * 10 + (uint32_t)(*c - '0');
        }
        uint32_t base = req->retry_backoff_ms ? req->retry_backoff_ms : 500u;
        uint32_t wait = digits ? (secs > 60 ? 60u : secs) * 1000u : backoff_for(base, as->attempt);
        uint64_t now = wtls_plat_now_ms();
        if (!(r->deadline_ms && (now >= r->deadline_ms || r->deadline_ms - now <= wait))) { /* else: this answer is the result */
            r->err = 0;
            r->eof = false;
            int rs = as_schedule_retry(r, as, wait, 0);
            return rs;
        }
    }
    const char *location = wtls_http_head_get(&r->head, "location");
    if (req->max_redirects >= 0 && is_redirect(code) && location) {
        struct async_state *saved = r->as;
        r->as = NULL; /* follow() may drain what has already arrived of the redirect body */
        st = follow(r, x, &as->u, location, &as->filtered);
        r->as = saved;
        if (st != WTLS_OK) return st;
        r->reused = false;
        as->fresh_only = false;
        as->got = false;
        wtls_free(r->buf);
        r->buf = NULL;
        memset(&r->head, 0, sizeof r->head);
        as->phase = AS_BEGIN;
        return WTLS_WANT_READ; /* run the next hop */
    }
    return WTLS_OK;
}

static int as_step_once(wtls_http_response *r, struct async_state *as) {
    request_ctx *x = &as->x;
    if (x->opt.abort_cb && x->opt.abort_cb(x->opt.abort_ctx)) {
        if (r->conn) tr_mark_aborted(r->conn);
        return WTLS_ERR_CANCELLED;
    }
    if (r->deadline_ms && wtls_plat_now_ms() >= r->deadline_ms) return WTLS_ERR_TIMEOUT;
    switch (as->phase) {
    case AS_BACKOFF:
        if (wtls_plat_now_ms() < as->retry_at_ms) {
            as->want = 1;
            return WTLS_WANT_READ;
        }
        as->fresh_only = false;
        as->got = false;
        as->phase = AS_BEGIN;
        return WTLS_WANT_READ;
    case AS_BEGIN: {
        int st = apply_deadline(x, &as->bt, r->deadline_ms);
        if (st != WTLS_OK) return st;
        cookie_hop_end(x, &as->ck); /* no-op unless an earlier attempt left it open */
#if WTLS_HTTP2
        as->h2_wait_over = false;
#endif
        cookie_hop_begin(x->req, x, &as->u, &as->ck);
        st = hop_begin(r, x, &as->u); /* the Cookie field stays in x->headers until the head is complete: HTTP/2 encodes it later */
        if (st != WTLS_OK) return st;
        x->cb_started = false;
        x->early_final = false;
        r->reused = false;
        as->phase = AS_ACQUIRE;
        return WTLS_WANT_READ;
    }
    case AS_ACQUIRE: {
        int st = as_acquire(r, as);
        if (st == WTLS_OK) return WTLS_WANT_READ; /* pooled: straight on */
        return st;
    }
    case AS_DNS: {
        int st = wtls_dns_job_poll(as->dns_job, as->addrs, &as->n_addrs);
        if (st == WTLS_WANT_READ) {
            if (as->dns_deadline_ms && wtls_plat_now_ms() >= as->dns_deadline_ms) return WTLS_ERR_TIMEOUT;
            return WTLS_WANT_READ;
        }
        as->dns_job = -1;
        if (st != WTLS_OK) return WTLS_ERR_DNS;
        as->addr_idx = 0;
        as->phase = AS_CONNECT;
        as->connect_deadline_ms = wtls_plat_now_ms() + (x->opt.connect_timeout_ms ? x->opt.connect_timeout_ms : DEF_CONNECT_MS);
        return WTLS_WANT_READ;
    }
    case AS_CONNECT: {
        int st = as_plain_connect(r, as);
        return st == WTLS_OK ? WTLS_WANT_READ : st;
    }
    case AS_TLS: {
        int st = wtls_connect_step(as->tls);
        if (st == WTLS_OK) {
            st = as_wrap_transport(r, as, as->tls, -1);
            return st == WTLS_OK ? WTLS_WANT_READ : st;
        }
        if (st == WTLS_WANT_READ || st == WTLS_WANT_WRITE) as->want = st == WTLS_WANT_READ ? 1 : 2;
        else {
            wtls_close(as->tls); /* a failed connection must be freed by us */
            as->tls = NULL;
#if WTLS_HTTP2
            h2_pend_remove(as);
#endif
        }
        return st;
    }
    case AS_SEND: {
        if (as->snd_len == 0 && as->snd_stage == 0) {
            int st = as_begin_send(r, as);
            if (st != WTLS_OK) return st;
        }
        int st = as_send(r, as);
        return st == WTLS_OK ? WTLS_WANT_READ : st;
    }
    case AS_HEAD: {
#if WTLS_HTTP2
        int st = r->h2st ? as_h2_read_head(r, as) : as_read_head(r, as);
#else
        int st = as_read_head(r, as);
#endif
        if (st != WTLS_OK) return st;
        return as_head_complete(r, as);
    }
#if WTLS_HTTP2
    case AS_H2WAIT: {
        h2_sess_gc(wtls_plat_now_ms());
        h2_sess *hs = h2_sess_find(r);
        if (hs) {
            as_h2_attach(r, as, hs);
            return WTLS_WANT_READ;
        }
        if (!h2_pend_find(r)) {
            /* the other connection turned out not to speak HTTP/2, or failed; or the session's streams are
             * all busy: give them H2_SLOT_WAIT_MS, then connect */
            if (!h2_full_find(r) || wtls_plat_now_ms() - as->h2_wait_since >= H2_SLOT_WAIT_MS) {
                if (h2_full_find(r)) as->h2_wait_over = true;
                as->phase = AS_ACQUIRE;
                return WTLS_WANT_READ;
            }
        }
        as->want = 1;
        return WTLS_WANT_READ;
    }
    case AS_H2SEND: {
        int st = as_h2_send(r, as);
        return st == WTLS_OK ? WTLS_WANT_READ : st;
    }
#else
    case AS_H2WAIT:
    case AS_H2SEND:
        break; /* not reachable without HTTP/2 */
#endif
    }
    return WTLS_ERR_INTERNAL;
}

/* Runs the machine until it must wait (WANT_* from the transport or a timer), failed, or finished. */
static int as_run(wtls_http_response *r, struct async_state *as) {
    for (int rounds = 0; rounds < 64; rounds++) {
        as_phase before = as->phase;
        int st = as_step_once(r, as);
        if (st == WTLS_OK) return WTLS_OK;
        if (st == WTLS_WANT_READ || st == WTLS_WANT_WRITE) {
            /* a new phase with WANT means "go on right away"; the same phase means "wait for the descriptor or the clock" */
            if (as->phase != before) continue;
            return st;
        }
        st = as_after_error(r, as, st);
        if (st == WTLS_WANT_READ) {
            if (as->phase == AS_BACKOFF) return WTLS_WANT_READ; /* wait out the backoff */
            continue;                                            /* the stale-connection retry */
        }
        return st;
    }
    return WTLS_WANT_READ; /* fair to the caller: the next step carries on */
}

wtls_status wtls_http_step(wtls_http_response *r) {
    if (!r) return WTLS_ERR_ARG;
    if (!WTLS_THREAD_GUARD("wtls_http_step")) return WTLS_ERR_STATE;
    struct async_state *as = r->as;
    if (!as) return r->err ? r->err : WTLS_OK; /* complete (or not an asynchronous response) */
    if (as->failed) return as->failed;
    int st = as_run(r, as);
    if (st == WTLS_OK) {
        request_ctx x = as->x; /* response_ready needs a few fields after the state is gone */
        st = response_ready(r, &x, x.req);
        if (st == WTLS_OK) {
            r->io_timeout_ms = as->bt.io;
            r->last_io_ms = wtls_plat_now_ms();
            r->as = NULL;
            x.head = NULL;
            async_free(as);
            return WTLS_OK;
        }
    }
    if (st != WTLS_WANT_READ && st != WTLS_WANT_WRITE) as->failed = st;
    return st;
}

wtls_status wtls_http_start(wtls_http_response **out, const wtls_http_request *req) {
    if (!out) return WTLS_ERR_ARG;
    *out = NULL;
    if (!req) return WTLS_ERR_ARG;
    if (!WTLS_THREAD_GUARD("wtls_http_start")) return WTLS_ERR_STATE;
    struct async_state *as = wtls_calloc(1, sizeof *as);
    if (!as) return WTLS_ERR_NOMEM;
    uint64_t deadline = req->total_timeout_ms ? wtls_plat_now_ms() + req->total_timeout_ms : 0;
    wtls_http_response *r;
    int st = request_setup(req, true, &r, &as->x, &as->u, &as->bt, deadline);
    if (st != WTLS_OK) {
        wtls_free(as);
        return st;
    }
    as->dns_job = -1;
    as->fd = -1;
    as->phase = AS_BEGIN;
    r->as = as;
    st = wtls_http_step(r);
    if (st != WTLS_OK && st != WTLS_WANT_READ && st != WTLS_WANT_WRITE) {
        wtls_http_close(r);
        return st;
    }
    *out = r;
    return st;
}

/* ---- waiting ------------------------------------------------------------------------ */

static int conn_fd(const wtls_http_response *r) {
#if WTLS_HTTP2
    if (r->h2sess) return wtls_get_fd(r->h2sess->tr->tls);
#endif
    if (!r->conn) return -1;
    return r->conn->tls ? wtls_get_fd(r->conn->tls) : r->conn->fd;
}

int wtls_http_get_fd(const wtls_http_response *r) {
    if (!r) return -1;
    const struct async_state *as = r->as;
    if (!as) return conn_fd(r);
    switch (as->phase) {
    case AS_CONNECT: return as->fd;
    case AS_TLS: return as->tls ? wtls_get_fd(as->tls) : -1;
    case AS_SEND:
    case AS_HEAD: return conn_fd(r);
#if WTLS_HTTP2
    case AS_H2SEND: return as->h2_hdr_sent ? conn_fd(r) : -1; /* waiting for a free slot is a matter of time, not of bytes */
#endif
    default: return -1; /* name lookup, backoff, between hops */
    }
}

bool wtls_http_pending(const wtls_http_response *r) {
    if (!r) return false;
    if (r->extra_off < r->extra_end) return true;
    if (r->conn && r->conn->tls && wtls_pending(r->conn->tls)) return true;
#if WTLS_HTTP2
    if (r->h2st && (h2s_stream_ready(r->h2st) || h2s_pending(r->h2sess->s) || wtls_pending(r->h2sess->tr->tls)))
        return true;
#endif
#if WTLS_CODECS
    if (r->gz && r->gz_off < r->gz_end) return true;
#endif
    return false;
}

int wtls_http_wait(wtls_http_response *const *rs, size_t n, uint32_t timeout_ms) {
    if (!rs || n == 0 || n > 64) return WTLS_ERR_ARG;
    int fds[64];
    uint8_t want[64], ready[64];
    bool timer[64];
    int immediate = 0;
    bool any_timer = false, any_fd = false;
    for (size_t i = 0; i < n; i++) {
        const wtls_http_response *r = rs[i];
        timer[i] = false;
        fds[i] = -1;
        want[i] = 1;
        ready[i] = 0;
        if (!r) continue;
        if (wtls_http_pending(r)) {
            ready[i] = 1;
            immediate++;
            continue;
        }
        fds[i] = wtls_http_get_fd(r);
        if (r->as) want[i] = r->as->want ? r->as->want : 1;
#if WTLS_HTTP2
        else if (r->h2sess) want[i] = (uint8_t)(1u | (h2s_wants(r->h2sess->s) & 2u));
#endif
        if (fds[i] < 0 && r->as) {
            timer[i] = true;
            any_timer = true;
        } else if (fds[i] >= 0) {
            any_fd = true;
        }
    }
    if (immediate) return immediate;
    if (any_timer) { /* something that needs stepping on a clock, not on a descriptor */
        uint32_t nap = timeout_ms < 2 ? timeout_ms : 2;
        int cnt = any_fd ? wtls_plat_poll_many(fds, want, n, nap, ready) : 0;
        if (!any_fd) wtls_plat_sleep_ms(nap);
        for (size_t i = 0; i < n; i++)
            if (timer[i] && !ready[i]) {
                ready[i] = 1;
                cnt++;
            }
        return cnt;
    }
    if (!any_fd) return 0;
    return wtls_plat_poll_many(fds, want, n, timeout_ms, ready);
}
