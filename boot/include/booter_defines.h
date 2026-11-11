// Ersatz für WiiFlows source/defines.h (siehe boot/UPSTREAM.md): der Booter braucht daraus nur den
// Patcher-Namen, den patchcode.c an Wiimmfi meldet. NL_VERSION kommt aus CMake (PROJECT_VERSION).
#ifndef NL_BOOTER_DEFINES_H
#define NL_BOOTER_DEFINES_H

#define APP_NAME "newlauncher"
#define APP_VERSION NL_VERSION

#endif
