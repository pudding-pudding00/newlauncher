// Test-DOL für den Chainloader (Spec 9, M3): meldet argc und jedes argv-Element über SYS_Report,
// das Dolphin mit OSREPORT_HLE ins Log schreibt. Zeigt die Werte zusätzlich auf dem Bildschirm.

#include <gccore.h>
#include <stdio.h>

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

    SYS_Report("NL_ARGV argc=%d\n", argc);
    printf("NL_ARGV argc=%d\n", argc);
    for (int i = 0; i < argc; ++i) {
        SYS_Report("NL_ARGV %d %s\n", i, argv[i] != NULL ? argv[i] : "(null)");
        printf("NL_ARGV %d %s\n", i, argv[i] != NULL ? argv[i] : "(null)");
    }
    for (;;) VIDEO_WaitVSync();
}
