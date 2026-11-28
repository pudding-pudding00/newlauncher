/* Host platform layer (tests only): Winsock on Windows, POSIX elsewhere. */
#include "plat.h"

#include <stdlib.h>
#include <string.h>

#include <wiitls/wiitls.h>

#if defined(WTLS_THREAD_CHECK) && WTLS_THREAD_CHECK
#include <stdio.h>
#endif
#if !defined(_WIN32)
#include <pthread.h>
#endif

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

typedef SOCKET sock_t;
#define SOCK_INVALID INVALID_SOCKET
#define sock_close closesocket
#define sock_poll WSAPoll
static int sock_err(void) { return WSAGetLastError(); }
#define ERR_WOULDBLOCK WSAEWOULDBLOCK
#define ERR_INPROGRESS WSAEWOULDBLOCK
#define ERR_INTR WSAEINTR
#define SEND_FLAGS 0

int wtls_plat_os_entropy(uint8_t *out, size_t len) {
    if (len > 0xFFFFFFFFu) return WTLS_ERR_ARG;
    NTSTATUS st = BCryptGenRandom(NULL, out, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return BCRYPT_SUCCESS(st) ? WTLS_OK : WTLS_ERR_ENTROPY;
}

bool wtls_plat_test_net_down;
bool wtls_plat_test_emulated;

bool wtls_plat_entropy_emulated(void) { return wtls_plat_test_emulated; }

int wtls_plat_init(void) {
    static bool done;
    if (wtls_plat_test_net_down) return WTLS_ERR_NET;
    if (done) return WTLS_OK;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return WTLS_ERR_NET;
    done = true;
    return WTLS_OK;
}

static int set_nonblocking(sock_t s) {
    u_long on = 1;
    return ioctlsocket(s, FIONBIO, &on) == 0 ? 0 : -1;
}

/* QueryPerformanceCounter: GetTickCount64 only advances every ~16 ms. */
void wtls_plat_sleep_ms(uint32_t ms) { Sleep(ms); }

static SRWLOCK s_async_lock = SRWLOCK_INIT;

void wtls_plat_async_lock(void) { AcquireSRWLockExclusive(&s_async_lock); }
void wtls_plat_async_unlock(void) { ReleaseSRWLockExclusive(&s_async_lock); }

typedef struct {
    void (*fn)(void *);
    void *arg;
} async_call;

static DWORD WINAPI async_main(LPVOID p) {
    async_call c = *(async_call *)p;
    free(p);
    c.fn(c.arg);
    return 0;
}

int wtls_plat_async_run(void (*fn)(void *), void *arg) {
    async_call *c = malloc(sizeof *c);
    if (!c) return WTLS_ERR_NOMEM;
    c->fn = fn;
    c->arg = arg;
    HANDLE h = CreateThread(NULL, 256u * 1024u, async_main, c, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (!h) {
        free(c);
        return WTLS_ERR_UNSUPPORTED; /* the lookup then runs inline */
    }
    CloseHandle(h); /* detached */
    return WTLS_OK;
}

uint64_t wtls_plat_now_ms(void) {
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (uint64_t)(now.QuadPart / (freq.QuadPart / 1000));
}

uint64_t wtls_plat_now_us(void) {
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    /* split to avoid overflowing now * 1e6 */
    uint64_t s = (uint64_t)(now.QuadPart / freq.QuadPart);
    uint64_t rem = (uint64_t)(now.QuadPart % freq.QuadPart);
    return s * 1000000u + rem * 1000000u / (uint64_t)freq.QuadPart;
}

#else /* POSIX */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#if defined(__APPLE__)
#include <stdlib.h>
#else
#include <sys/random.h>
#endif
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

typedef int sock_t;
#define SOCK_INVALID (-1)
#define sock_close close
#define sock_poll poll
static int sock_err(void) { return errno; }
#define ERR_WOULDBLOCK EAGAIN
#define ERR_INPROGRESS EINPROGRESS
#define ERR_INTR EINTR
#if defined(MSG_NOSIGNAL)
#define SEND_FLAGS MSG_NOSIGNAL
#else
#define SEND_FLAGS 0
#endif

int wtls_plat_os_entropy(uint8_t *out, size_t len) {
#if defined(__APPLE__)
    arc4random_buf(out, len);
    return WTLS_OK;
#else
    while (len > 0) {
        ssize_t n = getrandom(out, len, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return WTLS_ERR_ENTROPY;
        }
        out += n;
        len -= (size_t)n;
    }
    return WTLS_OK;
#endif
}

bool wtls_plat_test_net_down;
bool wtls_plat_test_emulated;

bool wtls_plat_entropy_emulated(void) { return wtls_plat_test_emulated; }

int wtls_plat_init(void) { return wtls_plat_test_net_down ? WTLS_ERR_NET : WTLS_OK; }

static int set_nonblocking(sock_t s) {
    int fl = fcntl(s, F_GETFL, 0);
    return (fl < 0 || fcntl(s, F_SETFL, fl | O_NONBLOCK) < 0) ? -1 : 0;
}

void wtls_plat_sleep_ms(uint32_t ms) {
    struct timespec ts = {(time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L};
    nanosleep(&ts, NULL);
}

static pthread_mutex_t s_async_mutex = PTHREAD_MUTEX_INITIALIZER;

void wtls_plat_async_lock(void) { pthread_mutex_lock(&s_async_mutex); }
void wtls_plat_async_unlock(void) { pthread_mutex_unlock(&s_async_mutex); }

typedef struct {
    void (*fn)(void *);
    void *arg;
} async_call;

static void *async_main(void *p) {
    async_call c = *(async_call *)p;
    free(p);
    c.fn(c.arg);
    return NULL;
}

int wtls_plat_async_run(void (*fn)(void *), void *arg) {
    async_call *c = malloc(sizeof *c);
    if (!c) return WTLS_ERR_NOMEM;
    c->fn = fn;
    c->arg = arg;
    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, 256u * 1024u);
    int rc = pthread_create(&t, &attr, async_main, c);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        free(c);
        return WTLS_ERR_UNSUPPORTED;
    }
    return WTLS_OK;
}

uint64_t wtls_plat_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

uint64_t wtls_plat_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}
#endif

uint64_t wtls_plat_poll_wait_us;

static bool is_wouldblock(int e) {
#if !defined(_WIN32) && defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
    if (e == EWOULDBLOCK) return true;
#endif
    return e == ERR_WOULDBLOCK;
}

int wtls_plat_resolve_all(const char *host, uint32_t *addrs, size_t cap, size_t *count) {
    if (count) *count = 0;
    if (!host || !*host || !addrs || cap == 0 || !count) return WTLS_ERR_DNS;
    if (wtls_plat_parse_ipv4(host, &addrs[0])) {
        *count = 1;
        return WTLS_OK;
    }
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return WTLS_ERR_DNS;
    size_t n = 0;
    for (struct addrinfo *p = res; p && n < cap; p = p->ai_next) {
        if (p->ai_family != AF_INET || !p->ai_addr) continue;
        uint32_t ip;
        memcpy(&ip, &((struct sockaddr_in *)p->ai_addr)->sin_addr, 4);
        bool dup = false;
        for (size_t i = 0; i < n; i++) {
            if (addrs[i] == ip) {
                dup = true;
                break;
            }
        }
        if (!dup) addrs[n++] = ip;
    }
    freeaddrinfo(res);
    if (n == 0) return WTLS_ERR_DNS;
    *count = n;
    return WTLS_OK;
}

int wtls_plat_resolve(const char *host, uint32_t *ipv4_be) {
    if (!ipv4_be) return WTLS_ERR_DNS;
    size_t n = 0;
    return wtls_plat_resolve_all(host, ipv4_be, 1, &n);
}

bool wtls_plat_net_changed(void) { return false; }

#if defined(WTLS_THREAD_CHECK) && WTLS_THREAD_CHECK
static bool s_thread_bound;
#if defined(_WIN32)
static DWORD s_owner_tid;
void wtls_plat_thread_bind(void) {
    s_owner_tid = GetCurrentThreadId();
    s_thread_bound = true;
}
bool wtls_plat_thread_ok(void) { return !s_thread_bound || GetCurrentThreadId() == s_owner_tid; }
#else
static pthread_t s_owner_tid;
void wtls_plat_thread_bind(void) {
    s_owner_tid = pthread_self();
    s_thread_bound = true;
}
bool wtls_plat_thread_ok(void) { return !s_thread_bound || pthread_equal(s_owner_tid, pthread_self()) != 0; }
#endif
void wtls_plat_thread_unbind(void) { s_thread_bound = false; }
void wtls_plat_report_thread_violation(const char *api) {
    fprintf(stderr, "libwiitls: thread violation in %s (single-thread contract broken)\n", api);
}
#else
void wtls_plat_thread_bind(void) {}
void wtls_plat_thread_unbind(void) {}
bool wtls_plat_thread_ok(void) { return true; }
void wtls_plat_report_thread_violation(const char *api) { (void)api; }
#endif

/* Polls one socket; restarts on EINTR with the remaining time. */
static int poll_one(sock_t s, short events, uint32_t timeout_ms, short *revents) {
    uint64_t deadline = wtls_plat_now_ms() + timeout_ms;
    for (;;) {
        struct pollfd p;
        p.fd = s;
        p.events = events;
        p.revents = 0;
        uint64_t now = wtls_plat_now_ms();
        int left = now >= deadline ? 0 : (int)(deadline - now);
        int r = sock_poll(&p, 1, left);
        if (r < 0 && sock_err() == ERR_INTR) continue;
        if (r < 0) return WTLS_ERR_IO;
        if (revents) *revents = p.revents;
        return r > 0 ? 1 : 0;
    }
}

int wtls_plat_connect_abortable(uint32_t ipv4_be, uint16_t port, uint32_t timeout_ms,
                                wtls_plat_abort_fn abort_cb, void *abort_ctx, int *fd_out) {
    if (!fd_out) return WTLS_ERR_ARG;
    *fd_out = -1;
    if (abort_cb && abort_cb(abort_ctx)) return WTLS_ERR_CANCELLED;
    sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == SOCK_INVALID) return WTLS_ERR_CONNECT;
#if defined(SO_NOSIGPIPE)
    {
        int on = 1;
        (void)setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
    }
#endif
    if (set_nonblocking(s) != 0) {
        sock_close(s);
        return WTLS_ERR_CONNECT;
    }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    memcpy(&sa.sin_addr, &ipv4_be, 4);
    if (connect(s, (struct sockaddr *)&sa, sizeof sa) != 0) {
        int e = sock_err();
        if (e != ERR_INPROGRESS && !is_wouldblock(e)) {
            sock_close(s);
            return WTLS_ERR_CONNECT;
        }
        uint64_t deadline = wtls_plat_now_ms() + timeout_ms;
        for (;;) {
            if (abort_cb && abort_cb(abort_ctx)) {
                sock_close(s);
                return WTLS_ERR_CANCELLED;
            }
            uint64_t now = wtls_plat_now_ms();
            if (now >= deadline) {
                sock_close(s);
                return WTLS_ERR_TIMEOUT;
            }
            uint64_t left = deadline - now;
            uint32_t slice = (abort_cb && left > WTLS_POLL_SLICE_MS) ? WTLS_POLL_SLICE_MS
                                                                     : (left > UINT32_MAX ? UINT32_MAX : (uint32_t)left);
            short rev = 0;
            uint64_t p0 = wtls_plat_now_us();
            int r = poll_one(s, POLLOUT, slice, &rev);
            wtls_plat_poll_wait_us += wtls_plat_now_us() - p0; /* counted as poll wait */
            if (r == 0) {
                if (abort_cb && wtls_plat_now_ms() < deadline) continue;
                sock_close(s);
                return WTLS_ERR_TIMEOUT;
            }
            int soerr = 0;
            socklen_t sl = sizeof soerr;
            if (r < 0 || getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&soerr, &sl) != 0 || soerr != 0) {
                sock_close(s);
                return WTLS_ERR_CONNECT;
            }
            break;
        }
    }
    int one = 1;
    (void)setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
    *fd_out = (int)s;
    return WTLS_OK;
}

int wtls_plat_connect(uint32_t ipv4_be, uint16_t port, uint32_t timeout_ms, int *fd_out) {
    return wtls_plat_connect_abortable(ipv4_be, port, timeout_ms, NULL, NULL, fd_out);
}

int wtls_plat_connect_begin(uint32_t ipv4_be, uint16_t port, int *fd_out) {
    if (!fd_out) return WTLS_ERR_ARG;
    *fd_out = -1;
    sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == SOCK_INVALID) return WTLS_ERR_CONNECT;
#if defined(SO_NOSIGPIPE)
    {
        int on = 1;
        (void)setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
    }
#endif
    if (set_nonblocking(s) != 0) {
        sock_close(s);
        return WTLS_ERR_CONNECT;
    }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    memcpy(&sa.sin_addr, &ipv4_be, 4);
    if (connect(s, (struct sockaddr *)&sa, sizeof sa) != 0) {
        int e = sock_err();
        if (e == ERR_INPROGRESS || is_wouldblock(e)) {
            *fd_out = (int)s;
            return WTLS_WANT_WRITE;
        }
        sock_close(s);
        return WTLS_ERR_CONNECT;
    }
    int one = 1;
    (void)setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
    *fd_out = (int)s;
    return WTLS_OK;
}

int wtls_plat_connect_check(int fd, uint32_t ipv4_be, uint16_t port) {
    (void)ipv4_be;
    (void)port;
    short rev = 0;
    int r = poll_one((sock_t)fd, POLLOUT, 0, &rev);
    if (r == 0) return WTLS_WANT_WRITE;
    if (r < 0) return WTLS_ERR_CONNECT;
    int soerr = 0;
    socklen_t sl = sizeof soerr;
    if (getsockopt((sock_t)fd, SOL_SOCKET, SO_ERROR, (char *)&soerr, &sl) != 0 || soerr != 0)
        return WTLS_ERR_CONNECT;
    int one = 1;
    (void)setsockopt((sock_t)fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
    return WTLS_OK;
}

long wtls_plat_test_send_block_after = -1;

int wtls_plat_send(int fd, const uint8_t *buf, size_t len) {
    if (len == 0) return WTLS_ERR_ARG;
    if (len > WTLS_PLAT_MAX_CHUNK) len = WTLS_PLAT_MAX_CHUNK;
    if (wtls_plat_test_send_block_after == 0) {
        wtls_plat_test_send_block_after = -1;
        return WTLS_WANT_WRITE; /* a full send buffer, once */
    }
    if (wtls_plat_test_send_block_after > 0 && (long)len > wtls_plat_test_send_block_after)
        len = (size_t)wtls_plat_test_send_block_after;
    for (;;) {
        int r = (int)send((sock_t)fd, (const char *)buf, (int)len, SEND_FLAGS);
        if (r > 0 && wtls_plat_test_send_block_after > 0) wtls_plat_test_send_block_after -= r;
        if (r > 0) return r;
        int e = sock_err();
        if (r < 0 && e == ERR_INTR) continue;
        if (r < 0 && is_wouldblock(e)) return WTLS_WANT_WRITE;
        return WTLS_ERR_IO;
    }
}

int wtls_plat_recv(int fd, uint8_t *buf, size_t len) {
    if (len == 0) return WTLS_ERR_ARG;
    if (len > WTLS_PLAT_MAX_CHUNK) len = WTLS_PLAT_MAX_CHUNK;
    for (;;) {
        int r = (int)recv((sock_t)fd, (char *)buf, (int)len, 0);
        if (r >= 0) return r;
        int e = sock_err();
        if (e == ERR_INTR) continue;
        if (is_wouldblock(e)) return WTLS_WANT_READ;
        return WTLS_ERR_IO;
    }
}

int wtls_plat_poll(int fd, bool rd, bool wr, uint32_t timeout_ms) {
    short ev = (short)((rd ? POLLIN : 0) | (wr ? POLLOUT : 0));
    uint64_t t0 = wtls_plat_now_us();
    int r = poll_one((sock_t)fd, ev, timeout_ms, NULL);
    wtls_plat_poll_wait_us += wtls_plat_now_us() - t0;
    return r;
}

void wtls_plat_close(int fd) {
    if (fd >= 0) sock_close((sock_t)fd);
}

int wtls_plat_udp_exchange(uint32_t ipv4_be, uint16_t port, const uint8_t *req, size_t req_len,
                           uint8_t *resp, size_t resp_cap, uint32_t timeout_ms) {
    sock_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == SOCK_INVALID) return WTLS_ERR_IO;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    memcpy(&sa.sin_addr, &ipv4_be, 4);
    int ret = WTLS_ERR_IO;
    if (sendto(s, (const char *)req, (int)req_len, 0, (struct sockaddr *)&sa, sizeof sa) ==
        (int)req_len) {
        int r = poll_one(s, POLLIN, timeout_ms, NULL);
        if (r == 0) {
            ret = WTLS_ERR_TIMEOUT;
        } else if (r > 0) {
            int n = (int)recv(s, (char *)resp, (int)resp_cap, 0);
            if (n >= 0) ret = n;
        }
    }
    sock_close(s);
    return ret;
}
