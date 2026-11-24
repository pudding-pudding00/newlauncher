/* Handshake profile of the last wtls_connect (internal; read by bench/ and the
 * tests). Recording costs two timebase reads per handshake step and per
 * send/recv, so it is always on.
 *
 * Wall time of a wtls_connect splits into
 *   connect_us                TCP connect (wtls_plat_connect), of which
 *                             connect_wait_us blocked in the OS / IOS poll
 *   setup_us                  config, SSL context, hostname, ticket offer
 *   step_us[state]            inside mbedtls_ssl_handshake_step, of which
 *                             step_io_us[state] in wtls_plat_send/recv (IPC)
 *   wait_us                   blocked in wtls_plat_poll between steps
 * plus the small rest (DNS cache, clock check, allocation, loop overhead). */
#ifndef WTLS_PROF_H
#define WTLS_PROF_H

#include <stdint.h>

/* Indexed by mbedtls_ssl_states; larger values share the last slot. */
#define WTLS_PROF_STATES 32

typedef struct {
    uint64_t connect_us;
    uint64_t connect_wait_us;
    uint64_t setup_us;
    uint64_t wait_us;
    uint64_t io_us;    /* all send/recv during the handshake steps */
    uint32_t io_calls;
    uint64_t send_us;  /* split of io_us: send, recv with data, recv without */
    uint32_t send_calls;
    uint64_t recv_us;
    uint32_t recv_calls;
    uint64_t recv_empty_us;
    uint32_t recv_empty_calls;
    uint64_t step_us[WTLS_PROF_STATES];
    uint64_t step_io_us[WTLS_PROF_STATES];
    uint32_t step_n[WTLS_PROF_STATES];
} wtls_prof;

/* Rewritten by every wtls_connect that reaches the TCP connect. */
extern wtls_prof wtls_prof_last;

#endif
