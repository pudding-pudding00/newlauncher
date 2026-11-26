#include "platform/ios_patch.hpp"

#include <gccore.h>
#include <ogc/machine/processor.h>

#include <cstring>

#include "platform/system.hpp"

namespace nl::platform {
namespace {

constexpr u32 kMem2ProtReg = 0x0D8B420A;  // Hollywood: MEM2-Schutz für den PPC; 0 = IOS-Speicher beschreibbar

// IOS liegt oben im MEM2. Gesucht wird über die gecachte Abbildung; vorher invalidieren, danach zurückschreiben.
constexpr u32 kSearchStart = 0x93A00000;
constexpr u32 kSearchEnd = 0x94000000;

// Rechteprüfung im FS-Modul (Thumb-Code): aus dem bedingten Sprung wird ein unbedingter.
// S1 offen: Muster ist aus öffentlichen Beschreibungen übernommen und auf IOS 58 der echten Wii noch nicht bestätigt.
constexpr u8 kFsPermOld[] = {0x42, 0x8B, 0xD0, 0x01, 0x25, 0x66};
constexpr u8 kFsPermNew[] = {0x42, 0x8B, 0xE0, 0x01, 0x25, 0x66};

bool runningInDolphin() {
    const s32 fd = IOS_Open("/dev/dolphin", IPC_OPEN_NONE);
    if (fd < 0) return false;
    IOS_Close(fd);
    return true;
}

u8* find(u8* begin, u8* end, const u8* pat, std::size_t n) {
    for (u8* p = begin; p + n <= end; ++p)
        if (std::memcmp(p, pat, n) == 0) return p;
    return nullptr;
}

}  // namespace

IosPatchResult applyIsfsPermissionPatch() {
    static bool done = false;  // Patch hält bis zum IOS-Neuladen; zweiter Aufruf meldet Already
    if (runningInDolphin()) return IosPatchResult::Emulated;
    if (!ahbprotDisabled()) return IosPatchResult::NoAhbprot;
    if (done) return IosPatchResult::Already;

    write16(kMem2ProtReg, 0);
    u8* const begin = reinterpret_cast<u8*>(kSearchStart);
    u8* const end = reinterpret_cast<u8*>(kSearchEnd);
    DCInvalidateRange(begin, kSearchEnd - kSearchStart);

    IosPatchResult result = IosPatchResult::NotFound;
    for (u8* p = begin; (p = find(p, end, kFsPermOld, sizeof kFsPermOld)) != nullptr; p += sizeof kFsPermOld) {
        std::memcpy(p, kFsPermNew, sizeof kFsPermNew);
        DCFlushRange(p, sizeof kFsPermNew);
        result = IosPatchResult::Applied;
    }
    if (result == IosPatchResult::NotFound && find(begin, end, kFsPermNew, sizeof kFsPermNew) != nullptr)
        result = IosPatchResult::Already;
    done = result != IosPatchResult::NotFound;
    return result;
}

const char* iosPatchName(IosPatchResult r) {
    switch (r) {
        case IosPatchResult::Applied: return "applied";
        case IosPatchResult::Already: return "already";
        case IosPatchResult::NotFound: return "notfound";
        case IosPatchResult::NoAhbprot: return "noahbprot";
        case IosPatchResult::Emulated: return "emulated";
    }
    return "?";
}

}  // namespace nl::platform
