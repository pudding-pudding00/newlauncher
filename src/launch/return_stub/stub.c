// Rückkehr-Stub (M20, Spec 4.3): lädt newlauncher nach exit() einer Homebrew-DOL von der SD neu.
// Frei stehend: keine libc, keine Interrupts, IPC von Hand. Liegt bei 0x80001800, der Parameterblock bei
// 0x80002800 (core/return_stub). Fortschrittscode bei 0x80002FF0; bei einem Fehler PI-Reset.

#include "stub_check.h"

typedef uint32_t u32;
typedef uint8_t u8;
typedef volatile u32 vu32;

#define UNCACHED(a) ((a) | 0x40000000u)  // 0x8/0x9… → 0xC/0xD…
#define PHYS(a) ((a) & 0x3FFFFFFFu)

// Hollywood-IPC (ungecacht), Bits in PPCCTRL: X1 = Anfrage, Y2 = Bestätigung, Y1 = Antwort, X2 = weiter.
#define IPC_PPCMSG (*(vu32*)0xCD000000u)
#define IPC_PPCCTRL (*(vu32*)0xCD000004u)
#define IPC_ARMMSG (*(vu32*)0xCD000008u)
#define HW_PPCIRQFLAG (*(vu32*)0xCD000030u)
#define CTRL_X1 0x01u
#define CTRL_Y2 0x02u
#define CTRL_Y1 0x04u
#define CTRL_X2 0x08u
#define CTRL_KEEP 0x30u  // IY1/IY2 unverändert lassen

#define IOS_OPEN 1u
#define IOS_CLOSE 2u
#define IOS_IOCTL 6u
#define IOS_IOCTLV 7u

// /dev/sdio (wie libogc wiisd.c)
#define SDIO_WRITEHCREG 0x01u
#define SDIO_READHCREG 0x02u
#define SDIO_RESETCARD 0x04u
#define SDIO_SETCLK 0x06u
#define SDIO_SENDCMD 0x07u
#define SDIO_GETSTATUS 0x0Bu
#define SDIO_HC_HOSTCONTROL 0x28u
#define SDIO_HC_4BIT 0x02u
#define SDIO_CMD_SELECT 0x07u
#define SDIO_CMD_SETBLOCKLEN 0x10u
#define SDIO_CMD_READMULTIBLOCK 0x12u
#define SDIO_TYPE_AC 3u
#define SDIO_RSP_R1 1u
#define SDIO_RSP_R1B 2u
#define SDIO_ST_INSERTED 0x00000001u
#define SDIO_ST_INITIALIZED 0x00010000u
#define SDIO_ST_SDHC 0x00100000u

#define CHUNK_SECTORS 64u  // 32 KiB je READ_MULTIPLE_BLOCK

// Arbeitsspeicher für IPC: ungecacht im Booter-Bereich (newlauncher hat ihn reserviert, der Puffer endet
// davor). Alles 32-Byte-ausgerichtet; IOS sieht nur physische Adressen.
#define SCRATCH 0x93300000u
#define REQ (SCRATCH + 0x000u)      // IPC-Anfrage, 0x20 Byte
#define VEC (SCRATCH + 0x040u)      // ioctlv-Vektoren, 3 × 8 Byte
#define SDREQ (SCRATCH + 0x080u)    // _sdiorequest, 9 Wörter
#define SDRSP (SCRATCH + 0x0C0u)    // _sdioresponse, 4 Wörter
#define BUFIN (SCRATCH + 0x100u)    // Eingabe für ioctl, 6 Wörter
#define BUFOUT (SCRATCH + 0x140u)   // Ausgabe für ioctl
#define PATH (SCRATCH + 0x180u)     // "/dev/sdio/slot0"
#define TIMEOUT_TICKS (60750000u * 3u)  // Zeitbasis = Busfrequenz / 4, 3 s je Aufruf

static vu32* uc(u32 addr) { return (vu32*)UNCACHED(addr); }

static void stage(u32 n);

static u32 timebase(void) {
    u32 t;
    __asm__ volatile("mftb %0" : "=r"(t));
    return t;
}

static __attribute__((noreturn)) void fail(void) {
    *(vu32*)0xCC003024u = 0;  // PI-Reset: Neustart ins Systemmenü
    for (;;) {
    }
}

static void ctrl(u32 bits) { IPC_PPCCTRL = (IPC_PPCCTRL & CTRL_KEEP) | bits; }

// Schickt REQ ab und wartet auf genau dessen Antwort; fremde Antworten (offene Anfragen des beendeten
// Programms) werden quittiert und verworfen. Liefert das IOS-Ergebnis.
static int ipc(void) {
    const u32 me = PHYS(REQ);
    u32 start = timebase();
    while (IPC_PPCCTRL & CTRL_X1) {
        if (timebase() - start > TIMEOUT_TICKS) fail();
    }
    IPC_PPCMSG = me;
    ctrl(CTRL_X1);
    start = timebase();
    for (;;) {
        const u32 c = IPC_PPCCTRL;
        if (c & CTRL_Y2) {  // wie libogc: Bit und IRQ-Flag quittieren, sonst kommt keine Antwort
            ctrl(CTRL_Y2);
            HW_PPCIRQFLAG = 0x40000000u;
        }
        if (c & CTRL_Y1) {
            const u32 msg = IPC_ARMMSG;
            ctrl(CTRL_Y1);
            HW_PPCIRQFLAG = 0x40000000u;
            ctrl(CTRL_X2);
            if (msg == me) return (int)uc(REQ)[1];
        }
        if (timebase() - start > TIMEOUT_TICKS) fail();
    }
}

static void req(u32 cmd, u32 fd, u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) {
    vu32* r = uc(REQ);
    r[0] = cmd;
    r[1] = 0;
    r[2] = fd;
    r[3] = a0;
    r[4] = a1;
    r[5] = a2;
    r[6] = a3;
    r[7] = a4;
}

static int ios_close(u32 fd) {
    req(IOS_CLOSE, fd, 0, 0, 0, 0, 0);
    return ipc();
}

static int ios_ioctl_at(u32 fd, u32 cmd, u32 in, u32 inLen, u32 out, u32 outLen) {
    req(IOS_IOCTL, fd, cmd, inLen ? PHYS(in) : 0, inLen, outLen ? PHYS(out) : 0, outLen);
    return ipc();
}

static int ios_ioctl(u32 fd, u32 cmd, u32 inLen, u32 outLen) { return ios_ioctl_at(fd, cmd, BUFIN, inLen, BUFOUT, outLen); }

// SENDCMD wie libogc: mit Datenpuffer oder bei SDHC als ioctlv (in = {Anfrage, Puffer}, io = {Antwort}),
// sonst als einfaches ioctl.
static int sd_cmd(u32 fd, int sdhc, u32 cmd, u32 rsp, u32 arg, u32 blocks, u32 buf) {
    vu32* s = uc(SDREQ);
    s[0] = cmd;
    s[1] = SDIO_TYPE_AC;
    s[2] = rsp;
    s[3] = arg;
    s[4] = blocks;
    s[5] = blocks ? 512u : 0u;
    s[6] = buf ? PHYS(buf) : 0u;
    s[7] = buf ? 1u : 0u;
    s[8] = 0;
    if (!buf && !sdhc) return ios_ioctl_at(fd, SDIO_SENDCMD, SDREQ, 36, SDRSP, 16);
    vu32* v = uc(VEC);
    v[0] = PHYS(SDREQ);
    v[1] = 36;
    v[2] = buf ? PHYS(buf) : 0u;
    v[3] = blocks * 512u;
    v[4] = PHYS(SDRSP);
    v[5] = 16;
    req(IOS_IOCTLV, fd, SDIO_SENDCMD, 2, 1, PHYS(VEC), 0);
    return ipc();
}

static void copyBytes(volatile u8* dst, const volatile u8* src, u32 n) {
    for (u32 i = 0; i < n; ++i) dst[i] = src[i];
}

static void zeroBytes(volatile u8* dst, u32 n) {
    for (u32 i = 0; i < n; ++i) dst[i] = 0;
}

static void syncRange(u32 addr, u32 size) {
    const u32 end = addr + size;
    for (u32 a = addr & ~31u; a < end; a += 32) {
        __asm__ volatile("dcbst 0,%0\n\tsync\n\ticbi 0,%0" : : "r"(a) : "memory");
    }
    __asm__ volatile("sync\n\tisync" : : : "memory");
}

// TRACE (vorübergehend): Dolphin loggt das Öffnen unbekannter Geräte; so wird die Stufe sichtbar.
static void stage(u32 n) {
    *uc(NLRS_STAGE_ADDR) = n;
}

void stub_main(void) {
    const unsigned char* p = (const unsigned char*)UNCACHED(NLRS_PARAMS_ADDR);
    stage(1);
    if (!nlrs_params_ok(p)) fail();
    const u32 dolSize = nlrs_be32(p + 0x08);
    const u32 count = nlrs_be32(p + 0x0C);
    const u32 argvSize = nlrs_be32(p + 0x10);
    const unsigned char* ext = nlrs_extents(p);

    // Offene Deskriptoren des beendeten Programms schließen (Fehler bei unbenutzten sind normal).
    stage(2);
    for (u32 fd = 0; fd < 32; ++fd) (void)ios_close(fd);

    stage(3);
    // Pfad direkt aus .rodata (MEM1): der Installer hat ihn mit DCFlushRange in den RAM geschrieben. Eine
    // byteweise Kopie in ungecachtes MEM2 kam in Dolphin leer bei IOS an.
    static const char path[] = "/dev/sdio/slot0";
    req(IOS_OPEN, 0, PHYS((u32)path), 0, 0, 0, 0);
    const int fdi = ipc();
    if (fdi < 0) fail();
    const u32 fd = (u32)fdi;

    stage(4);
    if (ios_ioctl(fd, SDIO_RESETCARD, 0, 4) < 0) fail();
    const u32 rca = uc(BUFOUT)[0] >> 16;
    if (ios_ioctl(fd, SDIO_GETSTATUS, 0, 4) < 0) fail();
    const u32 status = uc(BUFOUT)[0];
    if (!(status & SDIO_ST_INSERTED) || !(status & SDIO_ST_INITIALIZED)) fail();
    const int sdhc = (status & SDIO_ST_SDHC) != 0;

    // Host-Controller auf 1 Bit (die Karte steht nach RESETCARD ohnehin dort), Takt, Karte wählen.
    stage(5);
    vu32* in = uc(BUFIN);
    in[0] = SDIO_HC_HOSTCONTROL;
    in[1] = 0;
    in[2] = 0;
    in[3] = 1;
    in[4] = 0;
    in[5] = 0;
    if (ios_ioctl(fd, SDIO_READHCREG, 24, 4) < 0) fail();
    in[4] = (uc(BUFOUT)[0] & 0xFFu) & ~SDIO_HC_4BIT;
    if (ios_ioctl(fd, SDIO_WRITEHCREG, 24, 0) < 0) fail();
    in[0] = 1;
    if (ios_ioctl(fd, SDIO_SETCLK, 4, 0) < 0) fail();
    if (sd_cmd(fd, sdhc, SDIO_CMD_SELECT, SDIO_RSP_R1B, rca << 16, 0, 0) < 0) fail();
    if (sd_cmd(fd, sdhc, SDIO_CMD_SETBLOCKLEN, SDIO_RSP_R1, 512, 0, 0) < 0) fail();

    // DOL lesen: Extents in Dateireihenfolge, 32-KiB-Stücke direkt in den ungecachten Puffer.
    stage(6);
    u32 dst = NLRS_BUFFER_ADDR;
    for (u32 i = 0; i < count; ++i) {
        u32 lba = nlrs_be32(ext + i * 8u);
        u32 left = nlrs_be32(ext + i * 8u + 4u);
        while (left != 0) {
            const u32 n = left < CHUNK_SECTORS ? left : CHUNK_SECTORS;
            if (sd_cmd(fd, sdhc, SDIO_CMD_READMULTIBLOCK, SDIO_RSP_R1, sdhc ? lba : lba * 512u, n, dst) < 0) fail();
            dst += n * 512u;
            lba += n;
            left -= n;
        }
    }
    (void)ios_close(fd);

    stage(7);
    const u8* dol = (const u8*)UNCACHED(NLRS_BUFFER_ADDR);
    if (!nlrs_dol_ok(dol, dolSize)) fail();

    // Wie der App-Booter: BSS zuerst nullen (elf2dol fasst BSS grob zusammen), dann Sektionen kopieren.
    stage(8);
    const u32 bss = nlrs_be32(dol + 0xD8u);
    const u32 bssSize = nlrs_be32(dol + 0xDCu);
    if (bssSize != 0) {
        zeroBytes((volatile u8*)bss, bssSize);
        syncRange(bss, bssSize);
    }
    for (u32 i = 0; i < 18u; ++i) {
        const u32 off = nlrs_be32(dol + i * 4u);
        const u32 addr = nlrs_be32(dol + 0x48u + i * 4u);
        const u32 len = nlrs_be32(dol + 0x90u + i * 4u);
        if (len == 0) continue;
        copyBytes((volatile u8*)addr, dol + off, len);
        syncRange(addr, len);
    }

    // argv: libogc-crt0 = „b start; .long _arg; __system_argv (24 Byte)“; die Kommandozeile bleibt im
    // Parameterblock liegen, commandLine zeigt dorthin (core/return_stub, kReturnArgvAddr).
    const u32 entry = nlrs_be32(dol + 0xE0u);
    if (argvSize >= 24u && *(vu32*)(entry + 4u) == 0x5f617267u) {
        copyBytes((volatile u8*)(entry + 8u), (const u8*)NLRS_PARAMS_ADDR + NLRS_HEAD_BYTES, 24u);
        syncRange(entry + 8u, 24u);
        syncRange(NLRS_PARAMS_ADDR, NLRS_PARAMS_MAX);
    }

    stage(0);
    __asm__ volatile("mtctr %0\n\tbctr" : : "r"(entry));
    for (;;) {
    }
}
