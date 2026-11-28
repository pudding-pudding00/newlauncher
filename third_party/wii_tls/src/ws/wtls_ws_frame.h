/* WebSocket (RFC 6455) frame header parsing/building, masking and UTF-8
 * validation. Pure functions: no I/O, no allocation, no library state. */
#ifndef WTLS_WS_FRAME_H
#define WTLS_WS_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    WTLS_WS_OP_CONT = 0x0,
    WTLS_WS_OP_TEXT = 0x1,
    WTLS_WS_OP_BINARY = 0x2,
    WTLS_WS_OP_CLOSE = 0x8,
    WTLS_WS_OP_PING = 0x9,
    WTLS_WS_OP_PONG = 0xA,
};

#define WTLS_WS_MAX_HDR 14u
#define WTLS_WS_MAX_CONTROL 125u

typedef struct {
    bool fin;
    uint8_t opcode;
    bool masked;
    uint64_t len;     /* payload length */
    uint8_t mask[4];  /* valid when masked */
    size_t hdr_len;   /* header bytes consumed */
} wtls_ws_hdr;

/* Parses one frame header from buf[0..n). WTLS_OK (*h set), WTLS_WANT_READ (need more
 * bytes), or WTLS_ERR_WS: reserved bits set, reserved opcode, control frame longer than
 * 125 bytes or fragmented, 64-bit length with the top bit set, or a length that is not
 * in its shortest form. */
int wtls_ws_parse_header(const uint8_t *buf, size_t n, wtls_ws_hdr *h);

/* Writes a header into out; mask == NULL: unmasked. Returns the header length (2..14). */
size_t wtls_ws_build_header(uint8_t out[WTLS_WS_MAX_HDR], bool fin, uint8_t opcode, uint64_t len,
                            const uint8_t mask[4]);

/* XORs data with the mask, where data[0] is payload byte number offset. */
void wtls_ws_mask(uint8_t *data, size_t n, const uint8_t mask[4], uint64_t offset);

/* Strict UTF-8 (no overlongs, surrogates or code points above U+10FFFF). */
bool wtls_ws_valid_utf8(const uint8_t *s, size_t n);

/* A close code that may appear on the wire (RFC 6455 7.4). */
bool wtls_ws_close_code_valid(uint16_t code);

#endif
