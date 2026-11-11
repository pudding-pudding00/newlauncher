// Test-DOL für den Fortschrittsbalken des Booters (M11 Task 2, boot/nl/progress.c).
// Ruft die Funktionen in der Reihenfolge des Booters auf und prüft Pixel im Framebuffer:
// Balken 400×12 px, zentriert, 40 px über dem unteren Rand; Füllung weiß, Hintergrund schwarz.
// Meldet "NL_PROGRESS ok" oder "NL_PROGRESS fail <Grund>" über SYS_Report.

#include <gccore.h>
#include <stdio.h>

#include "nl/progress.h"

#define BLACK 0x00800080u
#define WHITE 0xFF80FF80u

static GXRModeObj* mode;

static u32 pixelPair(int x, int y) {
    const u32* fb = (const u32*)VIDEO_GetNextFramebuffer();
    DCInvalidateRange((void*)fb, 640 * 2 * mode->xfbHeight);
    return fb[(y * 640 + x) / 2];
}

static int barY(void) { return mode->xfbHeight - 40 - 12; }
static int fillRow(void) { return barY() + 6; }

static int check(int cond, const char* what) {
    if (!cond) SYS_Report("NL_PROGRESS fail %s\n", what);
    return cond;
}

int main(void) {
    VIDEO_Init();
    mode = VIDEO_GetPreferredMode(NULL);
    video_init();
    const int y = fillRow();
    int ok = 1;
    ok &= check(pixelPair(10, 10) == BLACK, "background");
    ok &= check(pixelPair(200, y) != WHITE, "empty-at-start");
    prog10();
    prog(20);
    ok &= check(pixelPair(124, y) == WHITE && pixelPair(140, y) != WHITE, "fill-at-30");
    setprog(320);
    ok &= check(pixelPair(130, y) == WHITE, "fill-at-32");
    ok &= check(pixelPair(400, y) != WHITE, "rest-at-32");
    setprog(1000);
    ok &= check(pixelPair(500, y) == WHITE, "full-at-1000");
    ok &= check(pixelPair(100, y) == BLACK, "left-of-bar");
    ok &= check(pixelPair(540, y) == BLACK, "right-of-bar");
    video_clear();
    ok &= check(pixelPair(300, y) == BLACK, "clear");
    if (ok) SYS_Report("NL_PROGRESS ok\n");
    for (;;) VIDEO_WaitVSync();
    return 0;
}
