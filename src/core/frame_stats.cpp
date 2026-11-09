#include "core/frame_stats.hpp"

#include <cstdio>

namespace nl {

int formatStats(const FrameStats& s, char* out, std::size_t cap) {
    if (cap == 0) return 0;
    const unsigned ms = s.cpuMicros / 1000u, tenth = (s.cpuMicros % 1000u) / 100u;
    int n;
    if (s.droppedQuads > 0) {
        n = std::snprintf(out, cap, "CPU %u.%u ms | DC %u | Q %u | Tex %u KB | DROP %u", ms, tenth,
                          static_cast<unsigned>(s.drawCalls), static_cast<unsigned>(s.quads),
                          static_cast<unsigned>(s.textureBytes / 1024u), static_cast<unsigned>(s.droppedQuads));
    } else {
        n = std::snprintf(out, cap, "CPU %u.%u ms | DC %u | Q %u | Tex %u KB", ms, tenth,
                          static_cast<unsigned>(s.drawCalls), static_cast<unsigned>(s.quads),
                          static_cast<unsigned>(s.textureBytes / 1024u));
    }
    if (n < 0) {
        out[0] = '\0';
        return 0;
    }
    return n < static_cast<int>(cap) ? n : static_cast<int>(cap) - 1;
}

}  // namespace nl
