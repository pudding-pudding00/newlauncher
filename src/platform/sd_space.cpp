#include "platform/sd_space.hpp"

#include <sys/statvfs.h>

namespace nl::platform {

u64 sdFreeBytes() {
    struct statvfs st{};
    if (statvfs("sd:/", &st) != 0) return 0;
    return u64{st.f_bfree} * u64{st.f_frsize ? st.f_frsize : st.f_bsize};
}

}  // namespace nl::platform
