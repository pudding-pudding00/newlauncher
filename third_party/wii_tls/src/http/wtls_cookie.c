/* Cookie jar. Single-threaded like the rest of the library. */
#include "http/wtls_cookie.h"

#include <string.h>

#include "wtls_mem.h"

#define COOKIE_MAX_BYTES 4096u /* name + value */
#define DEFAULT_MAX 64u
#define PATH_MAX_LEN 1024u
#define DOMAIN_MAX_LEN 255u

typedef struct {
    char *name, *value;
    char domain[DOMAIN_MAX_LEN + 1];
    char path[PATH_MAX_LEN + 1];
    bool host_only, secure, http_only;
    int64_t expires; /* 0: session cookie */
    uint64_t seq;    /* creation order */
} cookie;

struct wtls_cookie_jar {
    cookie *c;
    size_t n, max;
    uint64_t next_seq;
};

wtls_cookie_jar *wtls_cookie_jar_new(size_t max_cookies) {
    wtls_cookie_jar *j = wtls_calloc(1, sizeof *j);
    if (j) j->max = max_cookies ? max_cookies : DEFAULT_MAX;
    return j;
}

static void drop(cookie *c) {
    wtls_free(c->name);
    wtls_free(c->value);
}

void wtls_cookie_jar_clear(wtls_cookie_jar *j) {
    if (!j) return;
    for (size_t i = 0; i < j->n; i++) drop(&j->c[i]);
    j->n = 0;
}

void wtls_cookie_jar_free(wtls_cookie_jar *j) {
    if (!j) return;
    wtls_cookie_jar_clear(j);
    wtls_free(j->c);
    wtls_free(j);
}

size_t wtls_cookie_jar_count(const wtls_cookie_jar *j) { return j ? j->n : 0; }

/* ---- helpers --------------------------------------------------------------- */

static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static bool ieq_n(const char *a, size_t n, const char *lower) {
    if (strlen(lower) != n) return false;
    for (size_t i = 0; i < n; i++)
        if (lc(a[i]) != lower[i]) return false;
    return true;
}

static void trim(const char **s, size_t *n) {
    while (*n && (**s == ' ' || **s == '\t')) {
        (*s)++;
        (*n)--;
    }
    while (*n && ((*s)[*n - 1] == ' ' || (*s)[*n - 1] == '\t')) (*n)--;
}

static bool is_ip_literal(const char *h) {
    unsigned dots = 0;
    for (; *h; h++) {
        if (*h == '.') dots++;
        else if (*h < '0' || *h > '9') return false;
    }
    return dots == 3;
}

/* host == domain, or host ends in "." + domain */
static bool domain_match(const char *host, const char *domain) {
    size_t hl = strlen(host), dl = strlen(domain);
    if (hl == dl) return memcmp(host, domain, hl) == 0;
    return hl > dl && host[hl - dl - 1] == '.' && memcmp(host + hl - dl, domain, dl) == 0;
}

static bool path_match(const char *req, const char *cpath) {
    size_t cl = strlen(cpath);
    if (strncmp(req, cpath, cl) != 0) return false;
    return req[cl] == '\0' || cpath[cl - 1] == '/' || req[cl] == '/';
}

/* RFC 6265 5.1.4 default-path */
static void default_path(const char *req, char out[PATH_MAX_LEN + 1]) {
    size_t n = strlen(req);
    size_t last = 0;
    for (size_t i = 0; i < n; i++)
        if (req[i] == '/') last = i;
    if (n == 0 || req[0] != '/' || last == 0) {
        strcpy(out, "/");
        return;
    }
    if (last > PATH_MAX_LEN) last = PATH_MAX_LEN;
    memcpy(out, req, last);
    out[last] = '\0';
}

static int days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int)(era * 146097 + (int)doe - 719468);
}

/* ---- dates ------------------------------------------------------------------ */

int64_t wtls_cookie_parse_date(const char *s) {
    static const char *const months[] = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
    int hh = -1, mm = 0, ss = 0, day = -1, mon = -1;
    long year = -1;
    const char *p = s;
    while (*p) {
        while (*p && (*p == ' ' || *p == '\t' || *p == ',' || *p == ';' || *p == '-' || (*p >= '!' && *p <= '/' && *p != ':' && *p != '+') ))
            p++;
        const char *t = p;
        while (*p && *p != ' ' && *p != '\t' && *p != ',' && *p != ';' && *p != '-') p++;
        size_t n = (size_t)(p - t);
        if (!n) continue;
        if (hh < 0 && n >= 5 && t[0] >= '0' && t[0] <= '9') { /* h:m:s */
            int v[3] = {0, 0, 0}, k = 0;
            bool ok = true;
            for (size_t i = 0; i < n && ok; i++) {
                if (t[i] == ':') {
                    if (++k > 2) ok = false;
                } else if (t[i] >= '0' && t[i] <= '9') {
                    v[k] = v[k] * 10 + (t[i] - '0');
                    if (v[k] > 99) ok = false;
                } else {
                    ok = false;
                }
            }
            if (ok && k == 2 && v[0] < 24 && v[1] < 60 && v[2] < 61) {
                hh = v[0];
                mm = v[1];
                ss = v[2] > 59 ? 59 : v[2];
                continue;
            }
        }
        if (n >= 3 && mon < 0) {
            for (int i = 0; i < 12; i++)
                if (lc(t[0]) == months[i][0] && lc(t[1]) == months[i][1] && lc(t[2]) == months[i][2]) mon = i + 1;
            if (mon > 0) continue;
        }
        bool digits = true;
        for (size_t i = 0; i < n; i++) digits &= t[i] >= '0' && t[i] <= '9';
        if (digits && n <= 4) {
            long v = 0;
            for (size_t i = 0; i < n; i++) v = v * 10 + (t[i] - '0');
            if (day < 0 && n <= 2) day = (int)v;
            else if (year < 0 && n >= 2) year = v;
        }
    }
    if (year >= 70 && year <= 99) year += 1900;
    else if (year >= 0 && year <= 69) year += 2000;
    if (hh < 0 || day < 1 || day > 31 || mon < 0 || year < 1601) return -1;
    return (int64_t)days_from_civil(year, (unsigned)mon, (unsigned)day) * 86400 + hh * 3600 + mm * 60 + ss;
}

/* ---- storing ------------------------------------------------------------------ */

static void remove_at(wtls_cookie_jar *j, size_t i) {
    drop(&j->c[i]);
    j->c[i] = j->c[--j->n];
}

static bool same_key(const cookie *a, const cookie *b) {
    return strcmp(a->name, b->name) == 0 && strcmp(a->domain, b->domain) == 0 && strcmp(a->path, b->path) == 0 &&
           a->host_only == b->host_only;
}

static void purge_expired(wtls_cookie_jar *j, int64_t now) {
    for (size_t i = 0; i < j->n;) {
        if (j->c[i].expires && j->c[i].expires <= now) remove_at(j, i);
        else i++;
    }
}

int wtls_cookie_jar_set(wtls_cookie_jar *jar, const char *set_cookie, const char *host, const char *request_path,
                        bool secure_conn, int64_t now) {
    if (!jar || !set_cookie || !host || !request_path) return WTLS_ERR_ARG;
    const char *p = set_cookie;
    const char *semi = strchr(p, ';');
    size_t pair_len = semi ? (size_t)(semi - p) : strlen(p);
    const char *eq = memchr(p, '=', pair_len);
    if (!eq) return WTLS_ERR_ARG;
    const char *name = p, *value = eq + 1;
    size_t nl = (size_t)(eq - p), vl = pair_len - nl - 1;
    trim(&name, &nl);
    trim(&value, &vl);
    if (nl == 0 || nl + vl > COOKIE_MAX_BYTES) return WTLS_ERR_ARG;
    for (size_t i = 0; i < nl; i++)
        if ((unsigned char)name[i] < 0x21 || (unsigned char)name[i] == 0x7f) return WTLS_ERR_ARG;
    for (size_t i = 0; i < vl; i++)
        if ((unsigned char)value[i] < 0x20 || (unsigned char)value[i] == 0x7f) return WTLS_ERR_ARG;

    cookie c;
    memset(&c, 0, sizeof c);
    c.host_only = true;
    bool have_maxage = false;
    int64_t max_age_expiry = 0, expires_attr = 0;
    bool delete_now = false;
    char attr_path[PATH_MAX_LEN + 1] = "";
    char attr_domain[DOMAIN_MAX_LEN + 1] = "";
    bool have_domain = false;

    while (semi) {
        const char *a = semi + 1;
        const char *next = strchr(a, ';');
        size_t al = next ? (size_t)(next - a) : strlen(a);
        const char *aeq = memchr(a, '=', al);
        const char *an = a, *av = "";
        size_t anl = aeq ? (size_t)(aeq - a) : al, avl = 0;
        if (aeq) {
            av = aeq + 1;
            avl = al - anl - 1;
        }
        trim(&an, &anl);
        trim(&av, &avl);
        if (ieq_n(an, anl, "secure")) c.secure = true;
        else if (ieq_n(an, anl, "httponly")) c.http_only = true;
        else if (ieq_n(an, anl, "max-age") && avl) {
            bool neg = *av == '-';
            size_t i = neg ? 1 : 0;
            bool ok = i < avl;
            int64_t v = 0;
            for (; i < avl && ok; i++) {
                if (av[i] < '0' || av[i] > '9') ok = false;
                else if (v < INT64_MAX / 20) v = v * 10 + (av[i] - '0');
            }
            if (ok) {
                have_maxage = true;
                if (neg || v == 0) delete_now = true;
                else max_age_expiry = now + v;
            }
        } else if (ieq_n(an, anl, "expires")) {
            char tmp[64];
            if (avl < sizeof tmp) {
                memcpy(tmp, av, avl);
                tmp[avl] = '\0';
                int64_t t = wtls_cookie_parse_date(tmp);
                if (t >= 0) expires_attr = t ? t : 1;
            }
        } else if (ieq_n(an, anl, "path") && avl && av[0] == '/' && avl <= PATH_MAX_LEN) {
            memcpy(attr_path, av, avl);
            attr_path[avl] = '\0';
        } else if (ieq_n(an, anl, "domain") && avl) {
            if (av[0] == '.') {
                av++;
                avl--;
            }
            if (avl == 0 || avl > DOMAIN_MAX_LEN) return WTLS_ERR_ARG;
            for (size_t i = 0; i < avl; i++) attr_domain[i] = lc(av[i]);
            attr_domain[avl] = '\0';
            have_domain = true;
        }
        semi = next;
    }

    if (c.secure && !secure_conn) return WTLS_ERR_ARG; /* "leave secure cookies alone" */
    if (have_domain) {
        if (!strchr(attr_domain, '.') || is_ip_literal(attr_domain) || !domain_match(host, attr_domain))
            return WTLS_ERR_ARG;
        c.host_only = false;
        strcpy(c.domain, attr_domain);
    } else {
        size_t hl = strlen(host);
        if (hl == 0 || hl > DOMAIN_MAX_LEN) return WTLS_ERR_ARG;
        strcpy(c.domain, host);
    }
    if (attr_path[0]) strcpy(c.path, attr_path);
    else default_path(request_path, c.path);
    if (have_maxage) c.expires = delete_now ? 1 : max_age_expiry;
    else if (expires_attr) c.expires = expires_attr;
    bool expired = c.expires && c.expires <= now;

    c.name = wtls_malloc(nl + 1);
    c.value = wtls_malloc(vl + 1);
    if (!c.name || !c.value) {
        drop(&c);
        return WTLS_ERR_NOMEM;
    }
    memcpy(c.name, name, nl);
    c.name[nl] = '\0';
    memcpy(c.value, value, vl);
    c.value[vl] = '\0';

    size_t at = jar->n;
    for (size_t i = 0; i < jar->n; i++)
        if (same_key(&jar->c[i], &c)) {
            at = i;
            break;
        }
    if (at < jar->n) {
        c.seq = jar->c[at].seq; /* a replaced cookie keeps its creation order */
        drop(&jar->c[at]);
        jar->c[at] = jar->c[--jar->n];
    }
    if (expired) {
        drop(&c);
        return WTLS_OK; /* deletes */
    }
    if (!c.seq) c.seq = ++jar->next_seq;
    purge_expired(jar, now);
    if (jar->n >= jar->max) { /* evict the oldest */
        size_t oldest = 0;
        for (size_t i = 1; i < jar->n; i++)
            if (jar->c[i].seq < jar->c[oldest].seq) oldest = i;
        remove_at(jar, oldest);
    }
    cookie *nc = wtls_realloc(jar->c, (jar->n + 1) * sizeof *nc);
    if (!nc) {
        drop(&c);
        return WTLS_ERR_NOMEM;
    }
    jar->c = nc;
    jar->c[jar->n++] = c;
    return WTLS_OK;
}

/* ---- matching ------------------------------------------------------------------ */

char *wtls_cookie_jar_header(wtls_cookie_jar *jar, const char *host, const char *request_path, bool secure_conn,
                             int64_t now) {
    if (!jar || !host || !request_path) return NULL;
    purge_expired(jar, now);
    size_t idx[256], n = 0, total = 0;
    for (size_t i = 0; i < jar->n && n < sizeof idx / sizeof idx[0]; i++) {
        const cookie *c = &jar->c[i];
        if (c->secure && !secure_conn) continue;
        if (c->host_only ? strcmp(host, c->domain) != 0 : !domain_match(host, c->domain)) continue;
        if (!path_match(request_path, c->path)) continue;
        idx[n++] = i;
        total += strlen(c->name) + strlen(c->value) + 3;
    }
    if (!n) return NULL;
    for (size_t a = 1; a < n; a++) { /* insertion sort: longer path first, then older first */
        size_t k = idx[a], b = a;
        while (b > 0) {
            const cookie *x = &jar->c[idx[b - 1]], *y = &jar->c[k];
            size_t xl = strlen(x->path), yl = strlen(y->path);
            if (xl > yl || (xl == yl && x->seq < y->seq)) break;
            idx[b] = idx[b - 1];
            b--;
        }
        idx[b] = k;
    }
    char *out = wtls_malloc(total + 1);
    if (!out) return NULL;
    size_t o = 0;
    for (size_t a = 0; a < n; a++) {
        const cookie *c = &jar->c[idx[a]];
        if (a) {
            out[o++] = ';';
            out[o++] = ' ';
        }
        size_t nl = strlen(c->name), vl = strlen(c->value);
        memcpy(out + o, c->name, nl);
        o += nl;
        out[o++] = '=';
        memcpy(out + o, c->value, vl);
        o += vl;
    }
    out[o] = '\0';
    return out;
}
