// Fortschrittsbalken des Game-Booters (M11, Spec v2 3.2). Eigene Implementierung; ersetzt
// video_tinyload.c/h (GPLv2-only). Die Namen bleiben, damit die WiiFlow-Dateien nur das Include ändern.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void video_init(void);   // Framebuffer schwarz, VI auf das bevorzugte TV-Format, Balkenrahmen zeichnen
void video_clear(void);  // Framebuffer schwarz
void prog(int p);        // Fortschritt um p Einheiten erhöhen (gesamt 0..1000)
void prog10(void);       // prog(10)
void setprog(int p);     // Fortschritt absolut setzen (0..1000)

#ifdef __cplusplus
}
#endif
