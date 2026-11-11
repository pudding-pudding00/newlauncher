// Fortschrittsbalken des Game-Booters (M11 Task 2). Ein YUY2-Framebuffer 640 × Modushöhe; darauf ein
// Balken 400×12 px, waagrecht zentriert, 40 px über dem unteren Rand: Rahmen grau, Füllung weiß.
// Ein u32 im Framebuffer sind zwei Pixel, deshalb liegen alle x-Grenzen auf geraden Pixeln.
#include "nl/progress.h"

#include <gccore.h>

#define FB_WIDTH 640
#define BAR_W 400
#define BAR_H 12
#define BAR_BOTTOM_GAP 40
#define FRAME 2  // Rahmenstärke in Pixeln (waagrecht ein Pixelpaar, senkrecht zwei Zeilen)
#define PROG_MAX 1000

#define PROG_BLACK 0x00800080u
#define PROG_WHITE 0xFF80FF80u
#define PROG_GRAY 0x80808080u

static u32* xfb;
static u32 fbHeight;
static int progress;

static void fillRect(int x, int y, int w, int h, u32 color) {
    // x und w in Pixeln (gerade), y und h in Zeilen.
    for (int row = y; row < y + h; ++row) {
        u32* line = xfb + (row * FB_WIDTH + x) / 2;
        for (int i = 0; i < w / 2; ++i) line[i] = color;
    }
}

static int barX(void) { return (FB_WIDTH - BAR_W) / 2; }
static int barY(void) { return (int)fbHeight - BAR_BOTTOM_GAP - BAR_H; }

static void flush(void) {
    DCFlushRange(xfb, FB_WIDTH * 2 * fbHeight);
    VIDEO_Flush();
}

static void drawFill(void) {
    const int innerX = barX() + FRAME;
    const int innerW = BAR_W - 2 * FRAME;
    const int innerY = barY() + FRAME;
    const int innerH = BAR_H - 2 * FRAME;
    const int w = (innerW * progress / PROG_MAX) & ~1;
    fillRect(innerX, innerY, w, innerH, PROG_WHITE);
    fillRect(innerX + w, innerY, innerW - w, innerH, PROG_BLACK);
    flush();
}

static void clearAll(void) {
    for (u32 i = 0; i < FB_WIDTH / 2 * fbHeight; ++i) xfb[i] = PROG_BLACK;
}

void video_init(void) {
    GXRModeObj* mode = VIDEO_GetPreferredMode(NULL);
    fbHeight = mode->xfbHeight;
    xfb = (u32*)SYS_AllocateFramebuffer(mode);
    progress = 0;
    clearAll();
    fillRect(barX(), barY(), BAR_W, BAR_H, PROG_GRAY);
    DCFlushRange(xfb, FB_WIDTH * 2 * fbHeight);
    VIDEO_Configure(mode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (mode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
    drawFill();
}

void video_clear(void) {
    if (xfb == NULL) return;
    clearAll();
    flush();
}

void setprog(int p) {
    if (xfb == NULL) return;
    progress = p < 0 ? 0 : (p > PROG_MAX ? PROG_MAX : p);
    drawFill();
}

void prog(int p) { setprog(progress + p); }

void prog10(void) { prog(10); }
