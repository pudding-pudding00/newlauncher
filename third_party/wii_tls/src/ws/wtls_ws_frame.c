#include "ws/wtls_ws_frame.h"

#include <wiitls/wiitls.h>

int wtls_ws_parse_header(const uint8_t *buf, size_t n, wtls_ws_hdr *h) {
    if (n < 2) return WTLS_WANT_READ;
    uint8_t b0 = buf[0], b1 = buf[1];
    if (b0 & 0x70) return WTLS_ERR_WS; /* RSV1-3: no extension was negotiated */
    uint8_t op = b0 & 0x0f;
    if ((op > 2 && op < 8) || op > 0xA) return WTLS_ERR_WS;
    bool masked = (b1 & 0x80) != 0;
    uint64_t len = b1 & 0x7f;
    size_t pos = 2;
    if (len == 126) {
        if (n < pos + 2) return WTLS_WANT_READ;
        len = ((uint64_t)buf[2] << 8) | buf[3];
        if (len < 126) return WTLS_ERR_WS;
        pos += 2;
    } else if (len == 127) {
        if (n < pos + 8) return WTLS_WANT_READ;
        len = 0;
        for (int i = 0; i < 8; i++) len = (len << 8) | buf[2 + i];
        if ((len >> 63) || len < 65536) return WTLS_ERR_WS;
        pos += 8;
    }
    bool control = op >= 8;
    if (control && (len > WTLS_WS_MAX_CONTROL || !(b0 & 0x80))) return WTLS_ERR_WS;
    if (masked) {
        if (n < pos + 4) return WTLS_WANT_READ;
        for (int i = 0; i < 4; i++) h->mask[i] = buf[pos + (size_t)i];
        pos += 4;
    }
    h->fin = (b0 & 0x80) != 0;
    h->opcode = op;
    h->masked = masked;
    h->len = len;
    h->hdr_len = pos;
    return WTLS_OK;
}

size_t wtls_ws_build_header(uint8_t out[WTLS_WS_MAX_HDR], bool fin, uint8_t opcode, uint64_t len,
                            const uint8_t mask[4]) {
    size_t pos = 0;
    out[pos++] = (uint8_t)((fin ? 0x80 : 0) | (opcode & 0x0f));
    uint8_t mbit = mask ? 0x80 : 0;
    if (len < 126) {
        out[pos++] = (uint8_t)(mbit | len);
    } else if (len < 65536) {
        out[pos++] = (uint8_t)(mbit | 126);
        out[pos++] = (uint8_t)(len >> 8);
        out[pos++] = (uint8_t)len;
    } else {
        out[pos++] = (uint8_t)(mbit | 127);
        for (int i = 7; i >= 0; i--) out[pos++] = (uint8_t)(len >> (8 * i));
    }
    if (mask)
        for (int i = 0; i < 4; i++) out[pos++] = mask[i];
    return pos;
}

void wtls_ws_mask(uint8_t *data, size_t n, const uint8_t mask[4], uint64_t offset) {
    for (size_t i = 0; i < n; i++) data[i] ^= mask[(offset + i) & 3];
}

bool wtls_ws_valid_utf8(const uint8_t *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        uint8_t c = s[i];
        if (c < 0x80) {
            i++;
            continue;
        }
        size_t need;
        uint32_t cp, min;
        if ((c & 0xE0) == 0xC0) {
            need = 1;
            cp = c & 0x1F;
            min = 0x80;
        } else if ((c & 0xF0) == 0xE0) {
            need = 2;
            cp = c & 0x0F;
            min = 0x800;
        } else if ((c & 0xF8) == 0xF0) {
            need = 3;
            cp = c & 0x07;
            min = 0x10000;
        } else {
            return false;
        }
        if (n - i - 1 < need) return false;
        for (size_t k = 1; k <= need; k++) {
            if ((s[i + k] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (s[i + k] & 0x3F);
        }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        i += need + 1;
    }
    return true;
}

bool wtls_ws_close_code_valid(uint16_t code) {
    if (code >= 3000 && code <= 4999) return true;
    return (code >= 1000 && code <= 1003) || (code >= 1007 && code <= 1011);
}
