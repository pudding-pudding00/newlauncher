// C-Schnittstelle des Booters zum LZ77-Dekoder in src/core/lz77 (M11, Spec v2 3.2).
// Ersetzt boot/source/lz77.h; die Signaturen sind die, die ChannelHandler.cpp schon immer aufruft.
#pragma once

#include <gctypes.h>

#ifdef __cplusplus
extern "C" {
#endif

// 1, wenn der Puffer mit Typbyte 0x10/0x11 oder "LZ77" + Typbyte beginnt; sonst 0.
// Liest höchstens 5 Bytes und die Bytes 1..4 nur, wenn Byte 0 'L' ist.
int isLZ77compressed(const u8 *buffer);

// Entpackt length Bytes aus buffer. 0 = ok, *output (malloc, Aufrufer gibt mit free frei) und
// *outputLen gesetzt; -1 = Fehler, *output unverändert.
int decompressLZ77content(const u8 *buffer, u32 length, u8 **output, u32 *outputLen);

#ifdef __cplusplus
}
#endif
