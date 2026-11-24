/* TLS connection lifecycle: connect + handshake with deadlines, blocking
 * read/write over the non-blocking platform sockets. */
#include <ctype.h>
#include <limits.h>
#include <string.h>

#include <mbedtls/error.h>
#include <mbedtls/platform.h>
#include <mbedtls/x509.h>
#include <psa/crypto.h>

#include "plat/plat.h"
#include "wtls_castore.h"
#include "wtls_dns.h"
#include "wtls_scache.h"
#include "wtls_internal.h"
#include "wtls_prof.h"
#include "wtls_time.h"

#define DEF_CONNECT_MS 5000u
#define DEF_HANDSHAKE_MS 10000u
#define DEF_IO_MS 15000u

static uint32_t s_last_verify_flags;

wtls_prof wtls_prof_last;

/* Serialized session scratch (single-threaded API; keeps 4 KiB off LWP stacks). */
static uint8_t s_blob[WTLS_SESSION_BLOB_MAX];

uint32_t wtls_last_verify_flags(void) { return s_last_verify_flags; }

/* ---- BIO ---------------------------------------------------------------- */

/* Send/recv time of the running handshake step (wtls_prof). */
static uint64_t s_step_io_us;

static void prof_io(uint64_t t0, uint64_t *us, uint32_t *calls) {
    uint64_t dt = wtls_plat_now_us() - t0;
    s_step_io_us += dt;
    *us += dt;
    ++*calls;
    wtls_prof_last.io_calls++;
}

static int bio_send(void *ctx, const unsigned char *buf, size_t len) {
    wtls_conn *c = ctx;
    uint64_t t0 = wtls_plat_now_us();
    int r = wtls_plat_send(c->fd, buf, len);
    prof_io(t0, &wtls_prof_last.send_us, &wtls_prof_last.send_calls);
    if (r > 0) return r;
    if (r == WTLS_WANT_WRITE) return MBEDTLS_ERR_SSL_WANT_WRITE;
    c->bio_err = WTLS_ERR_IO;
    return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len) {
    wtls_conn *c = ctx;
    uint64_t t0 = wtls_plat_now_us();
    int r = wtls_plat_recv(c->fd, buf, len);
    if (r > 0) prof_io(t0, &wtls_prof_last.recv_us, &wtls_prof_last.recv_calls);
    else prof_io(t0, &wtls_prof_last.recv_empty_us, &wtls_prof_last.recv_empty_calls);
    if (r > 0) return r;
    if (r == 0) return MBEDTLS_ERR_SSL_CONN_EOF;
    if (r == WTLS_WANT_READ) return MBEDTLS_ERR_SSL_WANT_READ;
    c->bio_err = WTLS_ERR_IO;
    return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

/* ---- helpers ------------------------------------------------------------ */

/* Lower case, one trailing dot (the DNS root) removed. false if empty, too
 * long, or still ending in a dot ("host.." has an empty label). */
static bool normalize_host(const char *in, char out[256]) {
    size_t n = strlen(in);
    if (n > 0 && in[n - 1] == '.') n--;
    if (n == 0 || n > 253 || in[n - 1] == '.') return false;
    for (size_t i = 0; i < n; i++) out[i] = (char)tolower((unsigned char)in[i]);
    out[n] = 0;
    return true;
}

/* IP literal: no SNI (RFC 6066 §3), so the hostname stays NULL and the leaf is
 * checked against its iPAddress SANs here. */
static int verify_ip_san(void *ctx, mbedtls_x509_crt *crt, int depth, uint32_t *flags) {
    const wtls_conn *c = ctx;
    if (depth != 0) return 0;
    for (const mbedtls_x509_sequence *s = &crt->subject_alt_names; s && s->buf.p; s = s->next) {
        if ((s->buf.tag & MBEDTLS_ASN1_TAG_VALUE_MASK) == MBEDTLS_X509_SAN_IP_ADDRESS &&
            s->buf.len == 4 && memcmp(s->buf.p, &c->ipv4_be, 4) == 0)
            return 0;
    }
    *flags |= MBEDTLS_X509_BADCERT_CN_MISMATCH;
    return 0;
}

/* true if verification ran and set flags (0xFFFFFFFF: it did not run). */
static bool cert_rejected(const wtls_conn *c) {
    uint32_t f = mbedtls_ssl_get_verify_result(&c->ssl);
    return f != 0 && f != UINT32_MAX;
}

static int map_mbedtls(const wtls_conn *c, int r) {
    if (c->bio_err) return c->bio_err;
    switch (r) {
    case PSA_ERROR_INSUFFICIENT_MEMORY:
        return WTLS_ERR_NOMEM;
    case MBEDTLS_ERR_SSL_CONN_EOF:
        return WTLS_ERR_IO;
    default:
        return WTLS_ERR_HANDSHAKE;
    }
}

/* Waits for the direction mbedtls asked for until deadline. OK | TIMEOUT | IO. */
static int wait_io(const wtls_conn *c, int want, uint64_t deadline) {
    uint64_t now = wtls_plat_now_ms();
    if (now >= deadline) return WTLS_ERR_TIMEOUT;
    uint64_t left = deadline - now;
    int r = wtls_plat_poll(c->fd, want == MBEDTLS_ERR_SSL_WANT_READ, want == MBEDTLS_ERR_SSL_WANT_WRITE,
                           left > UINT32_MAX ? UINT32_MAX : (uint32_t)left);
    if (r > 0) return WTLS_OK;
    return r == 0 ? WTLS_ERR_TIMEOUT : WTLS_ERR_IO;
}

static void conn_free(wtls_conn *c) {
    mbedtls_ssl_free(&c->ssl);
    mbedtls_ssl_config_free(&c->conf);
    if (c->fd >= 0) wtls_plat_close(c->fd);
    mbedtls_free(c);
}

/* Offers a cached ticket for host:port. Any failure just means a full handshake. */
static void offer_ticket(wtls_conn *c) {
    size_t len;
    if (wtls_scache_take(c->host, c->port, s_blob, sizeof s_blob, &len, wtls_plat_now_ms()) != WTLS_OK) return;
    mbedtls_ssl_session sess;
    mbedtls_ssl_session_init(&sess);
    if (mbedtls_ssl_session_load(&sess, s_blob, len) == 0 && mbedtls_ssl_set_session(&c->ssl, &sess) == 0)
        c->offered_ticket = true;
    mbedtls_ssl_session_free(&sess);
    mbedtls_platform_zeroize(s_blob, len);
}

/* Called after MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET. */
static void store_ticket(wtls_conn *c) {
    if (c->no_resumption) return;
    mbedtls_ssl_session sess;
    mbedtls_ssl_session_init(&sess);
    size_t len = 0;
    if (mbedtls_ssl_get_session(&c->ssl, &sess) == 0 &&
        mbedtls_ssl_session_save(&sess, s_blob, sizeof s_blob, &len) == 0)
        wtls_scache_put(c->host, c->port, s_blob, len, wtls_plat_now_ms());
    mbedtls_ssl_session_free(&sess);
    mbedtls_platform_zeroize(s_blob, sizeof s_blob);
}

/* ---- public API --------------------------------------------------------- */

wtls_status wtls_connect(wtls_conn **out, const char *host, uint16_t port, const wtls_options *opt) {
    if (!out) return WTLS_ERR_ARG;
    *out = NULL;
    if (!host || port == 0) return WTLS_ERR_ARG;
    if (!wtls_core_initialized()) return WTLS_ERR_STATE;
    static const wtls_options k_defaults;
    if (!opt) opt = &k_defaults;

    char name[256];
    if (!normalize_host(host, name)) return WTLS_ERR_ARG;
    s_last_verify_flags = 0;
    if (!opt->insecure_skip_verify && wtls_time_check(wtls_plat_unix_time()) != WTLS_OK)
        return WTLS_ERR_CLOCK;

    uint32_t ip;
    bool literal = wtls_plat_parse_ipv4(name, &ip);
    if (!literal && wtls_dns_lookup(name, &ip, wtls_plat_now_ms()) != WTLS_OK) return WTLS_ERR_DNS;

    wtls_conn *c = mbedtls_calloc(1, sizeof *c);
    if (!c) return WTLS_ERR_NOMEM;
    c->fd = -1;
    mbedtls_ssl_init(&c->ssl);
    mbedtls_ssl_config_init(&c->conf);
    memcpy(c->host, name, strlen(name) + 1);
    c->port = port;
    c->ip_literal = literal;
    c->ipv4_be = ip;
    /* An unverified session must never be resumed by, or seed, a verified one:
     * resumption skips certificate verification. */
    c->no_resumption = opt->no_resumption || opt->insecure_skip_verify;
    c->io_timeout_ms = opt->io_timeout_ms ? opt->io_timeout_ms : DEF_IO_MS;

    memset(&wtls_prof_last, 0, sizeof wtls_prof_last);
    uint64_t p0 = wtls_plat_now_us(), w0 = wtls_plat_poll_wait_us;
    int st = wtls_plat_connect(ip, port, opt->connect_timeout_ms ? opt->connect_timeout_ms : DEF_CONNECT_MS,
                               &c->fd);
    if (st != WTLS_OK) {
        c->fd = -1;
        conn_free(c);
        if (!literal) wtls_dns_invalidate(name); /* the address may have moved */
        return (wtls_status)st;
    }
    uint64_t t0 = wtls_plat_now_ms();
    uint64_t p1 = wtls_plat_now_us();
    wtls_prof_last.connect_us = p1 - p0;
    wtls_prof_last.connect_wait_us = wtls_plat_poll_wait_us - w0;

    int r = wtls_conf_setup(&c->conf, opt);
    if (r == 0) r = mbedtls_ssl_setup(&c->ssl, &c->conf);
    if (r == 0) r = mbedtls_ssl_set_hostname(&c->ssl, literal ? NULL : c->host);
    if (r != 0) {
        conn_free(c);
        return r == MBEDTLS_ERR_SSL_ALLOC_FAILED || r == PSA_ERROR_INSUFFICIENT_MEMORY ? WTLS_ERR_NOMEM
                                                                                       : WTLS_ERR_INTERNAL;
    }
    if (literal && !opt->insecure_skip_verify) mbedtls_ssl_set_verify(&c->ssl, verify_ip_san, c);
    mbedtls_ssl_set_bio(&c->ssl, c, bio_send, bio_recv, NULL);
    if (!c->no_resumption) offer_ticket(c);

    c->lookups_before = wtls_castore_lookups;
    uint64_t deadline = t0 + (opt->handshake_timeout_ms ? opt->handshake_timeout_ms : DEF_HANDSHAKE_MS);
    wtls_prof_last.setup_us = wtls_plat_now_us() - p1;
    w0 = wtls_plat_poll_wait_us;
    for (;;) {
        /* mbedtls_ssl_handshake() is exactly this loop over steps; stepping
         * here lets wtls_prof time each handshake state. */
        r = 0;
        while (r == 0 && !mbedtls_ssl_is_handshake_over(&c->ssl)) {
            int state = c->ssl.MBEDTLS_PRIVATE(state); /* no public getter */
            int slot = state >= 0 && state < WTLS_PROF_STATES ? state : WTLS_PROF_STATES - 1;
            s_step_io_us = 0;
            uint64_t s0 = wtls_plat_now_us();
            r = mbedtls_ssl_handshake_step(&c->ssl);
            wtls_prof_last.step_us[slot] += wtls_plat_now_us() - s0;
            wtls_prof_last.step_io_us[slot] += s_step_io_us;
            wtls_prof_last.io_us += s_step_io_us;
            wtls_prof_last.step_n[slot]++;
        }
        if (r == 0) break;
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            st = wait_io(c, r, deadline);
            wtls_prof_last.wait_us = wtls_plat_poll_wait_us - w0;
            if (st == WTLS_OK) continue;
        } else if (r == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED || cert_rejected(c)) {
            /* The second case: rejected by the key-usage checks (e.g. a leaf
             * without serverAuth), which fail as BAD_CERTIFICATE but set flags. */
            s_last_verify_flags = mbedtls_ssl_get_verify_result(&c->ssl);
            st = WTLS_ERR_CERT;
        } else {
            st = map_mbedtls(c, r);
        }
        conn_free(c);
        return (wtls_status)st;
    }
    c->handshake_ms = (uint32_t)(wtls_plat_now_ms() - t0);
    *out = c;
    return WTLS_OK;
}

int wtls_read(wtls_conn *c, void *buf, size_t len) {
    if (!c || !buf || len == 0) return WTLS_ERR_ARG;
    if (len > INT_MAX) len = INT_MAX;
    uint64_t deadline = wtls_plat_now_ms() + c->io_timeout_ms;
    for (;;) {
        int r = mbedtls_ssl_read(&c->ssl, buf, len);
        if (r > 0) return r;
        switch (r) {
        case MBEDTLS_ERR_SSL_WANT_READ:
        case MBEDTLS_ERR_SSL_WANT_WRITE:
            if (c->nonblocking) return r == MBEDTLS_ERR_SSL_WANT_READ ? WTLS_WANT_READ : WTLS_WANT_WRITE;
            r = wait_io(c, r, deadline);
            if (r != WTLS_OK) return r;
            continue;
        case MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET:
            store_ticket(c);
            continue;
        case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY:
            return WTLS_ERR_CLOSED;
        case 0: /* transport EOF without close_notify */
        case MBEDTLS_ERR_SSL_CONN_EOF:
            return WTLS_ERR_IO;
        default:
            return c->bio_err ? c->bio_err : WTLS_ERR_IO;
        }
    }
}

int wtls_write(wtls_conn *c, const void *buf, size_t len) {
    if (!c || (!buf && len)) return WTLS_ERR_ARG;
    if (len > INT_MAX) len = INT_MAX;
    const unsigned char *p = buf;
    size_t off = 0;
    uint64_t deadline = wtls_plat_now_ms() + c->io_timeout_ms;
    while (off < len) {
        int r = mbedtls_ssl_write(&c->ssl, p + off, len - off);
        if (r > 0) {
            off += (size_t)r;
            continue;
        }
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (c->nonblocking)
                return off ? (int)off : (r == MBEDTLS_ERR_SSL_WANT_READ ? WTLS_WANT_READ : WTLS_WANT_WRITE);
            int st = wait_io(c, r, deadline);
            if (st != WTLS_OK) return off ? (int)off : st;
            continue;
        }
        if (r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
            store_ticket(c);
            continue;
        }
        if (off) return (int)off;
        return r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ? WTLS_ERR_CLOSED : (c->bio_err ? c->bio_err : WTLS_ERR_IO);
    }
    return (int)off;
}

int wtls_get_fd(const wtls_conn *c) { return c ? c->fd : -1; }

wtls_status wtls_get_info(const wtls_conn *c, wtls_conn_info *out) {
    if (!c || !out) return WTLS_ERR_ARG;
    out->tls_version = mbedtls_ssl_get_version(&c->ssl);
    out->ciphersuite = mbedtls_ssl_get_ciphersuite(&c->ssl);
    out->alpn = mbedtls_ssl_get_alpn_protocol(&c->ssl);
    out->resumed = c->offered_ticket && wtls_castore_lookups == c->lookups_before;
    out->handshake_ms = c->handshake_ms;
    return WTLS_OK;
}

void wtls_close(wtls_conn *c) {
    if (!c) return;
    if (!c->bio_err) mbedtls_ssl_close_notify(&c->ssl); /* best effort, one attempt */
    conn_free(c);
}

wtls_status wtls_set_nonblocking(wtls_conn *c, bool on) {
    if (!c) return WTLS_ERR_ARG;
    c->nonblocking = on;
    return WTLS_OK;
}

size_t wtls_pending(const wtls_conn *c) { return c ? mbedtls_ssl_get_bytes_avail(&c->ssl) : 0; }
