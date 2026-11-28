/* Platform abstraction: Wii (libogc) or host (POSIX/Winsock, tests only).
 * All returns are WTLS_* status codes or byte counts. Sockets are always
 * non-blocking after wtls_plat_connect. */
#ifndef WTLS_PLAT_H
#define WTLS_PLAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Largest single net_send/net_recv handed to IOS (shared 64 KiB IPC heap). */
#define WTLS_PLAT_MAX_CHUNK 16384
/* Poll slice when cooperative abort_cb is active so cancellation takes <= 100 ms. */
#define WTLS_POLL_SLICE_MS 100u
/* Maximum IPv4 addresses tried per DNS lookup during connect fallback. */
#define WTLS_DNS_MAX_ADDRS 4

/* Debug single-thread affinity guard (enabled automatically when !NDEBUG or
 * -DWTLS_THREAD_CHECK=ON). Zero cost in Release builds. */
#ifndef WTLS_THREAD_CHECK
#  if !defined(NDEBUG)
#    define WTLS_THREAD_CHECK 1
#  else
#    define WTLS_THREAD_CHECK 0
#  endif
#endif

typedef bool (*wtls_plat_abort_fn)(void *ctx);

/* Fills out with OS entropy. Host: BCryptGenRandom/getrandom. Wii: WTLS_ERR_ENTROPY. */
int wtls_plat_os_entropy(uint8_t *out, size_t len);

int      wtls_plat_init(void);                                         /* OK | ERR_NET */
/* Host-only test hook (defined in plat_host.c): true makes wtls_plat_init()
 * report WTLS_ERR_NET, as on a Wii before net_init(). */
extern bool wtls_plat_test_net_down;
/* True when the timing jitter comes from an emulator, where it carries no
 * entropy (Wii: Dolphin detected; host: the test hook below). */
bool     wtls_plat_entropy_emulated(void);
/* Host-only test hook (plat_host.c): makes wtls_plat_entropy_emulated() true. */
extern bool wtls_plat_test_emulated;
/* Host-only test hook (plat_host.c): >= 0 lets wtls_plat_send() pass this many more bytes, then answer
 * WTLS_WANT_WRITE once (a full send buffer) and reset itself to -1. */
extern long wtls_plat_test_send_block_after;
int      wtls_plat_resolve(const char *host, uint32_t *ipv4_be);       /* OK | ERR_DNS */
int      wtls_plat_resolve_all(const char *host, uint32_t *addrs,
                               size_t cap, size_t *count);             /* OK | ERR_DNS */
bool     wtls_plat_net_changed(void);
void     wtls_plat_thread_bind(void);
void     wtls_plat_thread_unbind(void);
bool     wtls_plat_thread_ok(void);
void     wtls_plat_report_thread_violation(const char *api);
int      wtls_plat_connect(uint32_t ipv4_be, uint16_t port, uint32_t timeout_ms,
                           int *fd_out);                               /* OK | ERR_CONNECT | ERR_TIMEOUT */
int      wtls_plat_connect_abortable(uint32_t ipv4_be, uint16_t port, uint32_t timeout_ms,
                                     wtls_plat_abort_fn abort_cb, void *abort_ctx,
                                     int *fd_out);                     /* OK | ERR_CONNECT | ERR_TIMEOUT | ERR_CANCELLED */
/* Non-blocking connect primitives used by wtls_connect_start()/wtls_connect_step(). */
int      wtls_plat_connect_begin(uint32_t ipv4_be, uint16_t port,
                                 int *fd_out);                         /* OK | WANT_WRITE | ERR_CONNECT */
int      wtls_plat_connect_check(int fd, uint32_t ipv4_be, uint16_t port); /* OK | WANT_WRITE | ERR_CONNECT */
int      wtls_plat_send(int fd, const uint8_t *buf, size_t len);       /* >0 | WANT_WRITE | ERR_IO */
int      wtls_plat_recv(int fd, uint8_t *buf, size_t len);             /* >0 | 0 (EOF) | WANT_READ | ERR_IO */
int      wtls_plat_poll(int fd, bool rd, bool wr, uint32_t timeout_ms);/* 1 ready | 0 timeout | ERR_IO */
/* Waits for any of n descriptors (want[i]: bit 0 read, bit 1 write; fds[i] < 0 is ignored) for at most timeout_ms.
 * ready[i] = 1 for every one that is ready or failed. Returns how many, 0 on timeout. */
int      wtls_plat_poll_many(const int *fds, const uint8_t *want, size_t n, uint32_t timeout_ms, uint8_t *ready);
void     wtls_plat_close(int fd);
int      wtls_plat_udp_exchange(uint32_t ipv4_be, uint16_t port, const uint8_t *req, size_t req_len,
                                uint8_t *resp, size_t resp_cap,
                                uint32_t timeout_ms);                  /* bytes | ERR_TIMEOUT | ERR_IO */
/* Runs fn(arg) once on a detached helper thread (used for DNS lookups). WTLS_OK, or WTLS_ERR_UNSUPPORTED where
 * no helper thread can take it: the caller then does the work itself. The lock below guards the
 * small shared tables such helpers use; without threads it does nothing. */
int      wtls_plat_async_run(void (*fn)(void *), void *arg);
void     wtls_plat_async_lock(void);
void     wtls_plat_async_unlock(void);
void     wtls_plat_sleep_ms(uint32_t ms);                              /* blocks the calling thread */
uint64_t wtls_plat_now_ms(void);                                       /* monotonic */
uint64_t wtls_plat_now_us(void);                                       /* monotonic, microseconds */
/* Total microseconds spent blocked inside wtls_plat_poll, and in the polls of a
 * pending wtls_plat_connect, since start-up. The
 * benchmark derives CPU time as wall time minus this wait time. */
extern uint64_t wtls_plat_poll_wait_us;
int64_t  wtls_plat_unix_time(void);                 /* time(NULL) + offset */
void     wtls_plat_set_unix_time_offset(int64_t delta_s);
int64_t  wtls_plat_unix_time_offset(void);          /* the delta set above (0 until SNTP ran) */

/* Strict dotted-quad IPv4 literal ("a.b.c.d", each 0..255, no leading '+'/space).
 * Returns true and stores the address in network byte order. */
bool wtls_plat_parse_ipv4(const char *s, uint32_t *ipv4_be);

/* IOS socket result classification. Used by plat_ogc.c; lives in
 * plat_common.c so the host tests cover it. libogc translates IOS results
 * into negated newlib errno values: EINPROGRESS (-119) and EISCONN (-127)
 * were measured on a Wii (docs/ios-socket-behaviour.md); EAGAIN and EALREADY
 * follow the same newlib numbering. */
#define WTLS_IOS_EAGAIN       11
#define WTLS_IOS_EALREADY    120
#define WTLS_IOS_EINPROGRESS 119
#define WTLS_IOS_EISCONN     127
#define WTLS_IPC_ENOMEM    (-22) /* libogc IPC heap exhausted: transient */
#define WTLS_IPC_RETRIES      5  /* short in-call retries before handing back WANT_* */
#define WTLS_PLAT_RETRY   (-1000) /* internal: pause briefly and repeat the call */

/* Maps a raw net_send/net_recv result: bytes | 0 (recv EOF) | WANT_READ/WRITE |
 * WTLS_PLAT_RETRY | ERR_IO. IPC exhaustion past the retries becomes WANT_*,
 * so the caller's wait loop keeps trying until its own deadline. */
int  wtls_plat_ios_io_result(int32_t r, bool is_send, int attempt);
/* true: a non-blocking connect returned an error that is resolved by polling
 * for writability and connecting again (any error, not only EINPROGRESS). */
bool wtls_plat_ios_connect_pending(bool nonblocking, int32_t r);
/* true: the second connect's result means the socket is connected. */
bool wtls_plat_ios_connected(int32_t r);
/* true: the second connect's result means the TCP handshake is still running. */
bool wtls_plat_ios_connect_in_progress(int32_t r);

#if defined(WTLS_PLATFORM_OGC)
/* One jitter sample: the timebase delta since the previous call, measured
 * across one IOS IPC round trip (see plat_ogc.c for the entropy estimate). */
uint32_t wtls_plat_jitter_sample(void);
/* Non-credited auxiliary hardware/network state mixed into conditioned entropy. */
void wtls_plat_entropy_aux(uint32_t aux[4]);
/* True when running in Dolphin (IOS_Open("/dev/dolphin") succeeds). */
bool wtls_plat_is_dolphin(void);
#endif

#endif
