// Test-DOL für den Chainloader (Spec 9, M3): meldet argc und jedes argv-Element über SYS_Report,
// das Dolphin mit OSREPORT_HLE ins Log schreibt. Zeigt die Werte zusätzlich auf dem Bildschirm.
// M8: Ist argv[1] ein NIN_CFG-Block, wird er dekodiert als NL_ARGV_NINCFG ausgegeben (statt der
// weiteren NL_ARGV-Zeilen, die an den NUL-Bytes des Blocks enden würden).

#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Die Fixture-Karte startet argv_echo als Nintendont; der Launcher sucht diese Versionsangabe.
static const char kVersionTag[] __attribute__((used)) = "$$Version:6.509";

#define NIN_CFG_SIZE 548
#define NIN_CFG_MAGIC 0x01070CF6u

static unsigned be32(const unsigned char* p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | (unsigned)p[3];
}

static void report(const char* line) {
    SYS_Report("%s\n", line);
    printf("%s\n", line);
}

static int echoNinCfg(const char* arg) {
    // Byteweise vergleichen: ein normales Argument endet an seinem NUL, vor dem memcpy über 548 Byte.
    const unsigned char* a = (const unsigned char*)arg;
    if (a[0] != 0x01 || a[1] != 0x07 || a[2] != 0x0C || a[3] != 0xF6) return 0;
    unsigned char b[NIN_CFG_SIZE];
    memcpy(b, arg, sizeof b);
    if (be32(b) != NIN_CFG_MAGIC) return 0;
    char path[256];
    memcpy(path, b + 0x14, 255);
    path[255] = '\0';
    char line[400];
    snprintf(line, sizeof line, "NL_ARGV_NINCFG ver=%u cfg=0x%08X vid=0x%08X lang=%d pads=%u mc=%u id=%.4s path=%s",
             be32(b + 0x04), be32(b + 0x08), be32(b + 0x0C), (int)be32(b + 0x10), be32(b + 0x214), (unsigned)b[0x21C],
             (const char*)(b + 0x218), path);
    report(line);
    return 1;
}

int main(int argc, char** argv) {
    VIDEO_Init();
    GXRModeObj* rmode = VIDEO_GetPreferredMode(NULL);
    void* xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();

    (void)*(volatile const char*)kVersionTag;  // Referenz: sonst entfernt --gc-sections den String trotz used

    // M20: Signatur des Rückkehr-Stubs bei 0x80001800 (+4 "STUBHAXX").
    const volatile unsigned* stub = (const volatile unsigned*)0x80001800;
    SYS_Report("NL_ARGV stub=%08x %08x %08x\n", stub[0], stub[1], stub[2]);
    SYS_Report("NL_ARGV argc=%d\n", argc);
    printf("NL_ARGV argc=%d\n", argc);
    const int nin = argc > 1 && argv[1] != NULL && echoNinCfg(argv[1]);
    for (int i = 0; i < (nin ? 1 : argc); ++i) {
        SYS_Report("NL_ARGV %d %s\n", i, argv[i] != NULL ? argv[i] : "(null)");
        printf("NL_ARGV %d %s\n", i, argv[i] != NULL ? argv[i] : "(null)");
    }
    // M20: letztes Argument "--exit" → nach kurzer Zeit exit(0); libogc springt dann in den Stub bei 0x80001800.
    if (!nin && argc > 1 && argv[argc - 1] != NULL && strcmp(argv[argc - 1], "--exit") == 0) {
        for (int i = 0; i < 60; ++i) VIDEO_WaitVSync();
        SYS_Report("NL_ARGV exit\n");
        exit(0);
    }
    for (;;) VIDEO_WaitVSync();
}
