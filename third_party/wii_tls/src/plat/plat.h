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

/* Fills out with OS entropy. Host: BCryptGenRandom/getrandom. Wii: WTLS_ERR_ENTROPY. */
int wtls_plat_os_entropy(uint8_t *out, size_t len);

int      wtls_plat_init(void);                                         /* OK | ERR_NET */
int      wtls_plat_resolve(const char *host, uint32_t *ipv4_be);       /* OK | ERR_DNS */
int      wtls_plat_connect(uint32_t ipv4_be, uint16_t port, uint32_t timeout_ms,
                           int *fd_out);                               /* OK | ERR_CONNECT | ERR_TIMEOUT */
int      wtls_plat_send(int fd, const uint8_t *buf, size_t len);       /* >0 | WANT_WRITE | ERR_IO */
int      wtls_plat_recv(int fd, uint8_t *buf, size_t len);             /* >0 | 0 (EOF) | WANT_READ | ERR_IO */
int      wtls_plat_poll(int fd, bool rd, bool wr, uint32_t timeout_ms);/* 1 ready | 0 timeout | ERR_IO */
void     wtls_plat_close(int fd);
int      wtls_plat_udp_exchange(uint32_t ipv4_be, uint16_t port, const uint8_t *req, size_t req_len,
                                uint8_t *resp, size_t resp_cap,
                                uint32_t timeout_ms);                  /* bytes | ERR_TIMEOUT | ERR_IO */
uint64_t wtls_plat_now_ms(void);                                       /* monotonic */
uint64_t wtls_plat_now_us(void);                                       /* monotonic, microseconds */
/* Total microseconds spent blocked inside wtls_plat_poll since start-up. The
 * benchmark derives CPU time as wall time minus this wait time. */
extern uint64_t wtls_plat_poll_wait_us;
int64_t  wtls_plat_unix_time(void);                 /* time(NULL) + offset */
void     wtls_plat_set_unix_time_offset(int64_t delta_s);

/* Strict dotted-quad IPv4 literal ("a.b.c.d", each 0..255, no leading '+'/space).
 * Returns true and stores the address in network byte order. */
bool wtls_plat_parse_ipv4(const char *s, uint32_t *ipv4_be);

/* IOS socket result classification. Used by plat_ogc.c; lives in
 * plat_common.c so the host tests cover it. libogc's net_* functions pass the
 * IOS result through _net_convert_error, so callers see negated newlib errno
 * values, not raw IOS numbers (newlauncher M11: Dolphin reported -127 =
 * -EISCONN for the second connect). Literal values so host builds, whose
 * errno.h differs, classify the same way. */
#define WTLS_IOS_EAGAIN       11  /* newlib EAGAIN */
#define WTLS_IOS_EALREADY    120  /* newlib EALREADY */
#define WTLS_IOS_EINPROGRESS 119  /* newlib EINPROGRESS */
#define WTLS_IOS_EISCONN     127  /* newlib EISCONN */
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

#if defined(WTLS_PLATFORM_OGC)
/* One timebase-jitter sample: delta of mftb across the disturbance chosen in
 * Task 2 (variant b: cache-hostile stride walk over a 256 KiB buffer). The
 * buffer is allocated on first use; wtls_plat_jitter_release() frees it.
 * Returns 0 forever if the buffer cannot be allocated (the health tests then
 * fail closed). */
uint32_t wtls_plat_jitter_sample(void);
void     wtls_plat_jitter_release(void);
#endif

#endif
