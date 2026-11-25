/* Wii platform layer: libogc net_* (IOS /dev/net/ip/top) and the timebase. */
#include "plat.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gccore.h>
#include <network.h>
#include <ogc/ipc.h>
#include <ogc/lwp.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/mutex.h>

#include <mbedtls/platform_time.h>
#include <wiitls/wiitls.h>

/* IOS socket error numbers (alphabetical SUSv3 order, as emulated by Dolphin's
 * IOS network code). libogc returns them negated, untranslated.
 * See docs/ios-socket-behaviour.md: confirmed by the Task 2 spike once run. */
/* IOS errno values and IPC_ENOMEM handling: plat.h (wtls_plat_ios_*). */
#define IPC_RETRY_US 2000
/* Poll slice while a non-blocking connect runs (two IPCs of ~135 us each). */
#define CONNECT_SLICE_MS 5u
/* IOS /dev/net/ip/top socket send buffer is ~4080 bytes, FIONSPACE /
 * SO_SNDLOWAT return -22, SO_SNDTIMEO returns -109, and IOS_O_NONBLOCK
 * corrupts streams on partial -EAGAIN even at 1024 B: cap single blocking
 * net_send calls at 2048 B after POLLOUT (aligned, halves window drift). */
#define OGC_SEND_MAX 2048u

/* IOS poll bits and fcntl flag. */
#define IOS_POLLIN  0x0003
#define IOS_POLLOUT 0x0008
#define IOS_F_GETFL 3
#define IOS_F_SETFL 4
#define IOS_O_NONBLOCK 0x04

int wtls_plat_os_entropy(uint8_t *out, size_t len) {
    (void)out;
    (void)len;
    return WTLS_ERR_ENTROPY; /* no OS entropy source on the Wii; see wtls_jitter */
}

/* MBEDTLS_PLATFORM_MS_TIME_ALT: monotonic milliseconds from the timebase. */
mbedtls_ms_time_t mbedtls_ms_time(void) {
    return (mbedtls_ms_time_t)ticks_to_millisecs(gettime());
}

uint64_t wtls_plat_now_ms(void) { return (uint64_t)ticks_to_millisecs(gettime()); }
uint64_t wtls_plat_now_us(void) { return (uint64_t)ticks_to_microsecs(gettime()); }

uint64_t wtls_plat_poll_wait_us;

static mutex_t g_dns_mutex = LWP_MUTEX_NULL;
static uint32_t s_last_host_ip;

int wtls_plat_init(void) {
    uint32_t ip = net_gethostip();
    if (ip == 0) return WTLS_ERR_NET; /* app must call net_init() first */
    s_last_host_ip = ip;
    if (g_dns_mutex == LWP_MUTEX_NULL && LWP_MutexInit(&g_dns_mutex, false) < 0)
        return WTLS_ERR_INTERNAL;
    return WTLS_OK;
}

bool wtls_plat_net_changed(void) {
    uint32_t ip = net_gethostip();
    if (ip != s_last_host_ip) {
        s_last_host_ip = ip;
        return true;
    }
    return false;
}

#if defined(WTLS_THREAD_CHECK) && WTLS_THREAD_CHECK
static lwp_t s_owner_lwp;
static bool s_thread_bound;
void wtls_plat_thread_bind(void) {
    s_owner_lwp = LWP_GetSelf();
    s_thread_bound = true;
}
void wtls_plat_thread_unbind(void) { s_thread_bound = false; }
bool wtls_plat_thread_ok(void) { return !s_thread_bound || LWP_GetSelf() == s_owner_lwp; }
void wtls_plat_report_thread_violation(const char *api) {
    SYS_Report("libwiitls: thread violation in %s (single-thread contract broken)\n", api);
}
#else
void wtls_plat_thread_bind(void) {}
void wtls_plat_thread_unbind(void) {}
bool wtls_plat_thread_ok(void) { return true; }
void wtls_plat_report_thread_violation(const char *api) { (void)api; }
#endif

int wtls_plat_resolve_all(const char *host, uint32_t *addrs, size_t cap, size_t *count) {
    if (count) *count = 0;
    if (!host || !*host || !addrs || cap == 0 || !count) return WTLS_ERR_DNS;
    if (wtls_plat_parse_ipv4(host, &addrs[0])) {
        *count = 1;
        return WTLS_OK;
    }
    int ret = WTLS_ERR_DNS;
    /* net_gethostbyname uses static buffers: serialize it. */
    if (g_dns_mutex != LWP_MUTEX_NULL) LWP_MutexLock(g_dns_mutex);
    struct hostent *he = net_gethostbyname(host);
    if (he && he->h_addrtype == AF_INET && he->h_length == 4 && he->h_addr_list) {
        size_t n = 0;
        for (size_t i = 0; he->h_addr_list[i] && n < cap; i++) {
            uint32_t ip;
            memcpy(&ip, he->h_addr_list[i], 4);
            bool dup = false;
            for (size_t j = 0; j < n; j++) {
                if (addrs[j] == ip) {
                    dup = true;
                    break;
                }
            }
            if (!dup) addrs[n++] = ip;
        }
        if (n > 0) {
            *count = n;
            ret = WTLS_OK;
        }
    }
    if (g_dns_mutex != LWP_MUTEX_NULL) LWP_MutexUnlock(g_dns_mutex);
    return ret;
}

int wtls_plat_resolve(const char *host, uint32_t *ipv4_be) {
    if (!ipv4_be) return WTLS_ERR_DNS;
    size_t n = 0;
    return wtls_plat_resolve_all(host, ipv4_be, 1, &n);
}

static int set_nonblocking(s32 fd) {
    s32 fl = net_fcntl(fd, IOS_F_GETFL, 0);
    if (fl < 0) fl = 0;
    return net_fcntl(fd, IOS_F_SETFL, (u32)fl | IOS_O_NONBLOCK) < 0 ? -1 : 0;
}

static int poll_one(s32 fd, u32 events, uint32_t timeout_ms) {
    struct pollsd p = {fd, events, 0};
    s32 r = net_poll(&p, 1, (s32)timeout_ms);
    if (r < 0) return WTLS_ERR_IO;
    return r > 0 ? 1 : 0;
}

int wtls_plat_connect_abortable(uint32_t ipv4_be, uint16_t port, uint32_t timeout_ms,
                                wtls_plat_abort_fn abort_cb, void *abort_ctx, int *fd_out) {
    if (!fd_out) return WTLS_ERR_ARG;
    *fd_out = -1;
    if (abort_cb && abort_cb(abort_ctx)) return WTLS_ERR_CANCELLED;
    s32 fd = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (fd < 0) return WTLS_ERR_CONNECT;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    memcpy(&sa.sin_addr, &ipv4_be, 4);

    /* Non-blocking connect + poll when IOS supports it; otherwise a blocking
     * connect whose timeout is only best effort (IOS' own SYN timeout). */
    bool nb = set_nonblocking(fd) == 0;
    s32 r = net_connect(fd, (struct sockaddr *)&sa, sizeof sa);
    if (wtls_plat_ios_connect_pending(nb, r)) {
        /* IOS' net_poll wakes for a refused connect (RST) but not for a
         * completed one: POLLOUT shows up only at the poll timeout (measured).
         * So poll in short slices and ask a second connect each time:
         * 0/EISCONN = connected, EALREADY/EINPROGRESS = still running. */
        uint64_t deadline = wtls_plat_now_ms() + timeout_ms;
        for (;;) {
            if (abort_cb && abort_cb(abort_ctx)) {
                net_close(fd);
                return WTLS_ERR_CANCELLED;
            }
            uint64_t now = wtls_plat_now_ms();
            if (now >= deadline) {
                net_close(fd);
                return WTLS_ERR_TIMEOUT;
            }
            uint64_t left = deadline - now;
            /* Counted as poll wait: the CPU sleeps while IOS waits. */
            uint64_t p0 = wtls_plat_now_us();
            int pr = poll_one(fd, IOS_POLLOUT, left < CONNECT_SLICE_MS ? (uint32_t)left : CONNECT_SLICE_MS);
            wtls_plat_poll_wait_us += wtls_plat_now_us() - p0;
            if (pr < 0) {
                r = -1;
                break;
            }
            r = net_connect(fd, (struct sockaddr *)&sa, sizeof sa);
            if (wtls_plat_ios_connected(r)) {
                r = 0;
                break;
            }
            if (!wtls_plat_ios_connect_in_progress(r)) {
                SYS_Report("wtls: connect failed, IOS code %d\n", (int)r);
                break;
            }
        }
    }
    if (r < 0) {
        net_close(fd);
        return WTLS_ERR_CONNECT;
    }
    if (!nb && set_nonblocking(fd) != 0) {
        net_close(fd);
        return WTLS_ERR_CONNECT;
    }
    /* No TCP_NODELAY: IOS rejects it with ENOPROTOOPT (-109, measured), so the
     * call would only cost an IPC round trip. */
    *fd_out = (int)fd;
    return WTLS_OK;
}

int wtls_plat_connect(uint32_t ipv4_be, uint16_t port, uint32_t timeout_ms, int *fd_out) {
    return wtls_plat_connect_abortable(ipv4_be, port, timeout_ms, NULL, NULL, fd_out);
}

int wtls_plat_send(int fd, const uint8_t *buf, size_t len) {
    if (len == 0) return WTLS_ERR_ARG;
    if (len > OGC_SEND_MAX) len = OGC_SEND_MAX;
    /* IOS /dev/net/ip/top has a ~4080-byte socket send buffer and a bug when
     * IOS_O_NONBLOCK is set: if len exceeds the remaining space, net_send still
     * queues the partial prefix into the TCP stream and returns -EAGAIN (-11),
     * so retrying duplicates those bytes. Check POLLOUT without blocking and
     * clear O_NONBLOCK around net_send so IOS never takes that path. */
    int pr = poll_one(fd, IOS_POLLOUT, 0);
    if (pr < 0) return WTLS_ERR_IO;
    if (pr == 0) return WTLS_WANT_WRITE;
    s32 fl = net_fcntl(fd, IOS_F_GETFL, 0);
    bool toggled = fl >= 0 && (fl & IOS_O_NONBLOCK) != 0 &&
                   net_fcntl(fd, IOS_F_SETFL, (u32)(fl & ~IOS_O_NONBLOCK)) == 0;
    int ret = WTLS_ERR_IO;
    for (int attempt = 0;; attempt++) {
        s32 r = net_send(fd, buf, (s32)len, 0);
        int c = wtls_plat_ios_io_result(r, true, attempt);
        if (c != WTLS_PLAT_RETRY) {
            ret = c;
            break;
        }
        usleep(IPC_RETRY_US);
    }
    if (toggled) net_fcntl(fd, IOS_F_SETFL, (u32)fl);
    return ret;
}

int wtls_plat_recv(int fd, uint8_t *buf, size_t len) {
    if (len == 0) return WTLS_ERR_ARG;
    if (len > WTLS_PLAT_MAX_CHUNK) len = WTLS_PLAT_MAX_CHUNK;
    for (int attempt = 0;; attempt++) {
        s32 r = net_recv(fd, buf, (s32)len, 0);
        int c = wtls_plat_ios_io_result(r, false, attempt);
        if (c != WTLS_PLAT_RETRY) return c;
        usleep(IPC_RETRY_US);
    }
}

int wtls_plat_poll(int fd, bool rd, bool wr, uint32_t timeout_ms) {
    uint64_t t0 = wtls_plat_now_us();
    int r = poll_one(fd, (rd ? IOS_POLLIN : 0) | (wr ? IOS_POLLOUT : 0), timeout_ms);
    wtls_plat_poll_wait_us += wtls_plat_now_us() - t0;
    return r;
}

void wtls_plat_close(int fd) {
    if (fd >= 0) net_close(fd);
}

int wtls_plat_udp_exchange(uint32_t ipv4_be, uint16_t port, const uint8_t *req, size_t req_len,
                           uint8_t *resp, size_t resp_cap, uint32_t timeout_ms) {
    s32 fd = net_socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (fd < 0) return WTLS_ERR_IO;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    memcpy(&sa.sin_addr, &ipv4_be, 4);
    int ret = WTLS_ERR_IO;
    /* IOS takes only its own 8-byte IPv4 sockaddr length: sizeof sa (16) fails
     * with -EINVAL (measured on a Wii). */
    if (net_sendto(fd, req, (s32)req_len, 0, (struct sockaddr *)&sa, 8) == (s32)req_len) {
        int pr = poll_one(fd, IOS_POLLIN, timeout_ms);
        if (pr == 0) {
            ret = WTLS_ERR_TIMEOUT;
        } else if (pr > 0) {
            s32 n = net_recvfrom(fd, resp, (s32)resp_cap, 0, NULL, NULL);
            if (n >= 0) ret = (int)n;
        }
    }
    net_close(fd);
    return ret;
}

/* ---- timebase jitter: one IOS IPC round trip per sample ---- */
/* The PPC timebase measured across a round trip into IOS on the Starlet,
 * whose timing depends on asynchronous events (Wi-Fi and timer interrupts,
 * the IOS scheduler). Measured on a Wii (tools/jitter_assess.py, worst
 * estimator): >= 4.6 bits of min-entropy per sample, versus 0.22 for
 * CPU-only cache-walk timing (docs/ios-socket-behaviour.md). IOS_Open of a
 * path that does not exist has no side effects and needs no network. */
static inline uint32_t tb_read(void) {
    uint32_t t;
    __asm__ volatile("mftb %0" : "=r"(t));
    return t;
}

uint32_t wtls_plat_jitter_sample(void) {
    static uint32_t prev;
    s32 fd = IOS_Open("/dev/wtls/none", 0);
    if (fd >= 0) IOS_Close(fd);
    uint32_t now = tb_read();
    uint32_t d = now - prev;
    prev = now;
    return d;
}
