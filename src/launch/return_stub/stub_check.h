/* Rückkehr-Stub (M20, Spec 4): Prüf-Logik in reinem C. Der Stub bindet sie frei stehend ein, die Host-Tests
 * und core/return_stub nutzen dieselben Konstanten und Funktionen. Kein libc außer <stdint.h>. */
#ifndef NL_RETURN_STUB_CHECK_H
#define NL_RETURN_STUB_CHECK_H

#include <stdint.h>

#define NLRS_STUB_ADDR 0x80001800u
#define NLRS_STUB_MAX 0x1000u
#define NLRS_PARAMS_ADDR 0x80002800u
#define NLRS_PARAMS_MAX 0x7F0u
#define NLRS_STAGE_ADDR 0x80002FF0u /* Fortschrittscode des Stubs, newlauncher loggt ihn beim Start */
#define NLRS_HEAD_BYTES 0x18u
#define NLRS_MAGIC 0x4E4C5253u /* "NLRS" */
#define NLRS_VERSION 1u
#define NLRS_ARGV_MAX 256u
#define NLRS_BUFFER_ADDR 0x92800000u /* Lesepuffer für die DOL, wie kDolBufferAddr */
#define NLRS_MAX_DOL 0x00B00000u     /* 11 MiB, wie kMaxDolBytes */
#define NLRS_MEM1_BEGIN 0x80003F00u  /* hinter den Systemvariablen */
#define NLRS_MEM1_END 0x81800000u
#define NLRS_MEM2_BEGIN 0x90000000u

static inline uint32_t nlrs_be32(const unsigned char* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Summe der big-endian-Wörter; n wird auf ganze Wörter abgerundet. */
static inline uint32_t nlrs_checksum(const unsigned char* p, uint32_t n) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i + 4 <= n; i += 4) sum += nlrs_be32(p + i);
    return sum;
}

static inline uint32_t nlrs_argv_padded(uint32_t argvSize) { return (argvSize + 3u) & ~3u; }

/* Gesamtgröße laut Kopf; 0, wenn die Felder schon für sich unsinnig sind. */
static inline uint32_t nlrs_params_size(const unsigned char* p) {
    const uint32_t count = nlrs_be32(p + 0x0C);
    const uint32_t argv = nlrs_be32(p + 0x10);
    if (argv > NLRS_ARGV_MAX || count > NLRS_PARAMS_MAX / 8u) return 0;
    return NLRS_HEAD_BYTES + nlrs_argv_padded(argv) + count * 8u;
}

static inline const unsigned char* nlrs_extents(const unsigned char* p) {
    return p + NLRS_HEAD_BYTES + nlrs_argv_padded(nlrs_be32(p + 0x10));
}

/* 1, wenn der Block vollständig und stimmig ist: Magic, Version, Größe, Prüfsumme, Extents decken genau
 * ceil(dolSize / 512) Sektoren ab, keines ist leer. */
static inline int nlrs_params_ok(const unsigned char* p) {
    if (nlrs_be32(p) != NLRS_MAGIC || nlrs_be32(p + 0x04) != NLRS_VERSION) return 0;
    const uint32_t dolSize = nlrs_be32(p + 0x08);
    const uint32_t count = nlrs_be32(p + 0x0C);
    const uint32_t size = nlrs_params_size(p);
    if (size == 0 || size > NLRS_PARAMS_MAX || count == 0) return 0;
    if (dolSize == 0 || dolSize > NLRS_MAX_DOL) return 0;
    if (nlrs_checksum(p + NLRS_HEAD_BYTES, size - NLRS_HEAD_BYTES) != nlrs_be32(p + 0x14)) return 0;
    const unsigned char* e = nlrs_extents(p);
    uint32_t sectors = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t n = nlrs_be32(e + i * 8u + 4u);
        if (n == 0 || n > NLRS_MAX_DOL / 512u) return 0;
        sectors += n;
    }
    return sectors == (dolSize + 511u) / 512u;
}

/* Zielbereich [addr, addr + size) liegt ganz in MEM1 hinter den Systemvariablen oder in MEM2 vor dem
 * Lesepuffer; nie über Stub, Parameterblock oder Puffer. */
static inline int nlrs_range_ok(uint32_t addr, uint32_t size) {
    const uint32_t end = addr + size;
    if (end < addr) return 0;
    if (addr >= NLRS_MEM1_BEGIN && end <= NLRS_MEM1_END) return 1;
    if (addr >= NLRS_MEM2_BEGIN && end <= NLRS_BUFFER_ADDR) return 1;
    return 0;
}

/* 1, wenn Kopf, Sektionen, BSS und Einsprung einer DOL mit `size` Byte zu laden sind. */
static inline int nlrs_dol_ok(const unsigned char* dol, uint32_t size) {
    if (size < 0x100u) return 0;
    for (uint32_t i = 0; i < 18u; ++i) {
        const uint32_t off = nlrs_be32(dol + i * 4u);
        const uint32_t addr = nlrs_be32(dol + 0x48u + i * 4u);
        const uint32_t len = nlrs_be32(dol + 0x90u + i * 4u);
        if (len == 0) continue;
        if (off + len < off || off + len > size) return 0;
        if (!nlrs_range_ok(addr, len)) return 0;
    }
    const uint32_t bss = nlrs_be32(dol + 0xD8u);
    const uint32_t bssSize = nlrs_be32(dol + 0xDCu);
    if (bssSize != 0 && !nlrs_range_ok(bss, bssSize)) return 0;
    return nlrs_range_ok(nlrs_be32(dol + 0xE0u), 4u);
}

#endif
