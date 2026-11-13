/* newlauncher: Build-Kompatibilität für libntfs mit aktuellem libogc/newlib (M16-Spike, Patch 3 der Liste).
 * Wird per -include vor jede libntfs-Quelle gesetzt; ändert keinen Upstream-Code. */
#pragma once
#include <gcbool.h>
#include <sys/types.h>
#ifndef makedev
#define makedev(ma, mi) ((dev_t)((((ma) & 0xffffu) << 16) | ((mi) & 0xffffu)))
#endif
