#include "launch/channel.hpp"

#include <gccore.h>
#include <ogc/wiilaunch.h>

#include <cstdio>

namespace nl::launch {

Result<void> launchChannel(u64 titleId) {
    char context[48];
    if (const s32 r = WII_Initialize(); r < 0) {
        std::snprintf(context, sizeof context, "WII_Initialize %d", static_cast<int>(r));
        return Result<void>::err({ErrorCode::IoError, context});
    }
    const s32 r = WII_LaunchTitle(titleId);  // kehrt bei Erfolg nicht zurück
    std::snprintf(context, sizeof context, "WII_LaunchTitle %d", static_cast<int>(r));
    return Result<void>::err({ErrorCode::IoError, context});
}

}  // namespace nl::launch
