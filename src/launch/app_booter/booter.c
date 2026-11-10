// App-Booter (Spec 9, M3): läuft frei stehend bei 0x93300000, nachdem der Launcher sich abgemeldet hat.
// Kopiert die Ziel-DOL aus dem Puffer an ihre Adressen, nullt BSS, reicht den argv-Block weiter und
// springt zum Einsprung. Keine libc, keine Globals: nur Stack (crt0.S) und der Kopf vor kArgsAddr.

typedef unsigned int u32;
typedef unsigned char u8;

#define ARGS_ADDR 0x93300800u
#define ARGV_MAGIC 0x5f617267u  // "_arg"
#define ARGV_HEADER_BYTES 24u

struct BootHeader {  // vom Chainloader bei ARGS_ADDR - 0x10 abgelegt
    u32 dolAddr, dolSize, argvAddr, argvSize;
};

struct DolHeader {
    u32 offset[18];
    u32 address[18];
    u32 size[18];
    u32 bssAddress, bssSize, entry;
};

// Datencache zurückschreiben und Befehlscache verwerfen, damit die CPU den neuen Code sieht.
static void syncRange(u32 addr, u32 size) {
    const u32 end = addr + size;
    for (u32 a = addr & ~31u; a < end; a += 32) {
        __asm__ volatile("dcbst 0,%0\n\tsync\n\ticbi 0,%0" : : "r"(a) : "memory");
    }
    __asm__ volatile("sync\n\tisync" : : : "memory");
}

static void copyBytes(volatile u8* dst, const volatile u8* src, u32 n) {
    for (u32 i = 0; i < n; ++i) dst[i] = src[i];
}

static void zeroBytes(volatile u8* dst, u32 n) {
    for (u32 i = 0; i < n; ++i) dst[i] = 0;
}

void booter_main(void) {
    const struct BootHeader* boot = (const struct BootHeader*)(ARGS_ADDR - 0x10u);
    const u8* dol = (const u8*)boot->dolAddr;
    const struct DolHeader* h = (const struct DolHeader*)dol;

    for (u32 i = 0; i < 18; ++i) {
        if (h->size[i] == 0) continue;
        copyBytes((volatile u8*)h->address[i], dol + h->offset[i], h->size[i]);
        syncRange(h->address[i], h->size[i]);
    }
    if (h->bssSize != 0) {
        zeroBytes((volatile u8*)h->bssAddress, h->bssSize);
        syncRange(h->bssAddress, h->bssSize);
    }

    const u32 entry = h->entry;
    // libogc-crt0: „b start; .long _arg; __system_argv (24 Byte)“. Die Kommandozeile bleibt hinter
    // dem Kopf bei ARGS_ADDR + 24 liegen, commandLine zeigt bereits dorthin.
    if (boot->argvSize >= ARGV_HEADER_BYTES && *(volatile u32*)(entry + 4) == ARGV_MAGIC) {
        copyBytes((volatile u8*)(entry + 8), (const u8*)boot->argvAddr, ARGV_HEADER_BYTES);
        syncRange(entry + 8, ARGV_HEADER_BYTES);
        syncRange(boot->argvAddr, boot->argvSize);
    }

    __asm__ volatile("mtctr %0\n\tbctr" : : "r"(entry));
    for (;;) {
    }
}
