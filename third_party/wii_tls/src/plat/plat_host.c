/* Host platform layer (tests only): Winsock on Windows, POSIX elsewhere. */
#include "plat.h"

#include <string.h>

#include <wiitls/wiitls.h>

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

int wtls_plat_init(void) {
    static bool done;
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
#include <sys/random.h>
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
}

int wtls_plat_init(void) { return WTLS_OK; }

static int set_nonblocking(sock_t s) {
    int fl = fcntl(s, F_GETFL, 0);
    return (fl < 0 || fcntl(s, F_SETFL, fl | O_NONBLOCK) < 0) ? -1 : 0;
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

int wtls_plat_resolve(const char *host, uint32_t *ipv4_be) {
    if (!host || !*host || !ipv4_be) return WTLS_ERR_DNS;
    if (wtls_plat_parse_ipv4(host, ipv4_be)) return WTLS_OK;
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return WTLS_ERR_DNS;
    memcpy(ipv4_be, &((struct sockaddr_in *)res->ai_addr)->sin_addr, 4);
    freeaddrinfo(res);
    return WTLS_OK;
}

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

int wtls_plat_connect(uint32_t ipv4_be, uint16_t port, uint32_t timeout_ms, int *fd_out) {
    if (!fd_out) return WTLS_ERR_ARG;
    *fd_out = -1;
    sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == SOCK_INVALID) return WTLS_ERR_CONNECT;
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
        short rev = 0;
        int r = poll_one(s, POLLOUT, timeout_ms, &rev);
        if (r == 0) {
            sock_close(s);
            return WTLS_ERR_TIMEOUT;
        }
        int soerr = 0;
        socklen_t sl = sizeof soerr;
        if (r < 0 || getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&soerr, &sl) != 0 || soerr != 0) {
            sock_close(s);
            return WTLS_ERR_CONNECT;
        }
    }
    int one = 1;
    (void)setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
    *fd_out = (int)s;
    return WTLS_OK;
}

int wtls_plat_send(int fd, const uint8_t *buf, size_t len) {
    if (len == 0) return WTLS_ERR_ARG;
    if (len > WTLS_PLAT_MAX_CHUNK) len = WTLS_PLAT_MAX_CHUNK;
    for (;;) {
        int r = (int)send((sock_t)fd, (const char *)buf, (int)len, SEND_FLAGS);
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
