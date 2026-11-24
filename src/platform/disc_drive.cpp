#include "platform/disc_drive.hpp"

#include <cstring>

#include <gccore.h>
#include <ogc/cache.h>
#include <ogc/ipc.h>
#include <ogc/lwp_watchdog.h>
#include <unistd.h>

namespace nl::platform {

namespace {

constexpr u32 kStackBytes = 64 * 1024;
constexpr u8 kPriority = 40;
constexpr u32 kTimeoutMs = 5000;
constexpr u32 kPollUs = 5000;

constexpr s32 kIoctlDiReadId = 0x70;
constexpr s32 kIoctlDiRead = 0x71;
constexpr s32 kIoctlDiGetCover = 0x88;
constexpr s32 kIoctlDiReset = 0x8A;
constexpr s32 kIoctlDiUnencRead = 0x8D;
constexpr u32 kCoverDiscInserted = 0x02;
constexpr u32 kHeaderBytes = 0x440;

struct AsyncWait {
    volatile bool done = false;
    volatile s32 result = -1;
};

// Statische DMA-Puffer für Starlet (32-Byte-ausgerichtet); solange ein vorheriges IOCTL nach einem Timeout
// noch aussteht, verhindert g_ioInFlight einen neuen Start auf denselben Puffern.
alignas(32) u32 g_inBuf[8]{};
alignas(32) u32 g_outBuf[8]{};
alignas(32) u8 g_hdrBuf[kHeaderBytes]{};
AsyncWait g_wait{};
volatile bool g_ioInFlight = false;

s32 diCallback(s32 result, void* usr) {
    auto* w = static_cast<AsyncWait*>(usr);
    w->result = result;
    w->done = true;
    g_ioInFlight = false;
    return 0;
}

s32 diIoctlTimed(s32 fd, s32 cmd, void* outBuf, s32 outLen, u64 deadlineTicks) {
    if (g_ioInFlight) return -1;
    if (gettime() >= deadlineTicks) return -1;
    g_wait.done = false;
    g_wait.result = -1;
    g_ioInFlight = true;
    DCFlushRange(g_inBuf, sizeof g_inBuf);
    if (outBuf != nullptr && outLen > 0) DCInvalidateRange(outBuf, static_cast<u32>(outLen));
    const s32 rc = IOS_IoctlAsync(fd, cmd, g_inBuf, sizeof g_inBuf, outBuf, outLen, &diCallback, &g_wait);
    if (rc < 0) {
        g_ioInFlight = false;
        return rc;
    }
    while (!g_wait.done) {
        if (gettime() >= deadlineTicks) return -1;
        usleep(kPollUs);
    }
    if (outBuf != nullptr && outLen > 0) DCInvalidateRange(outBuf, static_cast<u32>(outLen));
    return g_wait.result;
}

}  // namespace

DiscDrive::DiscDrive() { LWP_MutexInit(&mutex_, false); }

DiscDrive::~DiscDrive() {
    shutdown();
    LWP_MutexDestroy(mutex_);
}

void DiscDrive::setFakeDisc(std::optional<OpticalDisc> fake) { fake_ = std::move(fake); }

void DiscDrive::shutdown() { join(); }

void DiscDrive::join() {
    if (thread_ != LWP_THREAD_NULL) {
        LWP_JoinThread(thread_, nullptr);
        thread_ = LWP_THREAD_NULL;
    }
}

bool DiscDrive::requestRead(bool userInitiated) {
    if (running_) return false;
    join();
    userInitiated_ = userInitiated;
    running_ = true;
    if (LWP_CreateThread(&thread_, &DiscDrive::entry, this, nullptr, kStackBytes, kPriority) != 0) {
        running_ = false;
        thread_ = LWP_THREAD_NULL;
        return false;
    }
    return true;
}

std::optional<DiscReadResult> DiscDrive::poll(bool* userInitiated) {
    if (running_ || !hasResult_) return std::nullopt;
    LWP_MutexLock(mutex_);
    std::optional<DiscReadResult> out = std::move(result_);
    const bool manual = resultUserInitiated_;
    hasResult_ = false;
    LWP_MutexUnlock(mutex_);
    join();
    if (userInitiated != nullptr) *userInitiated = manual;
    return out;
}

DiscReadResult DiscDrive::readSync() {
    join();
    hasResult_ = false;
    return readHardware();
}

void* DiscDrive::entry(void* arg) {
    auto* self = static_cast<DiscDrive*>(arg);
    DiscReadResult r = self->readHardware();
    LWP_MutexLock(self->mutex_);
    self->result_ = std::move(r);
    self->resultUserInitiated_ = self->userInitiated_;
    self->hasResult_ = true;
    self->running_ = false;
    LWP_MutexUnlock(self->mutex_);
    return nullptr;
}

DiscReadResult DiscDrive::readHardware() {
    if (fake_) {
        if (fake_->kind == DiscKind::Wii || fake_->kind == DiscKind::Gc) return {DiscReadStatus::Ok, *fake_};
        if (fake_->kind == DiscKind::Unknown) return {DiscReadStatus::Unreadable, {}};
        return {DiscReadStatus::Empty, {}};
    }
    if (g_ioInFlight) return {DiscReadStatus::Unreadable, {}};

    const s32 fd = IOS_Open("/dev/di", 0);
    if (fd < 0) return {DiscReadStatus::Unreadable, {}};

    const u64 deadline = gettime() + millisecs_to_ticks(kTimeoutMs);
    auto finish = [&](DiscReadResult res) {
        IOS_Close(fd);
        return res;
    };

    std::memset(g_inBuf, 0, sizeof g_inBuf);
    std::memset(g_outBuf, 0, sizeof g_outBuf);
    g_inBuf[0] = static_cast<u32>(kIoctlDiGetCover) << 24;
    if (diIoctlTimed(fd, kIoctlDiGetCover, g_outBuf, sizeof g_outBuf, deadline) != 1) {
        return finish({DiscReadStatus::Unreadable, {}});
    }
    if ((g_outBuf[0] & kCoverDiscInserted) == 0) {
        return finish({DiscReadStatus::Empty, {}});
    }

    std::memset(g_inBuf, 0, sizeof g_inBuf);
    g_inBuf[0] = static_cast<u32>(kIoctlDiReset) << 24;
    g_inBuf[1] = 1;
    if (diIoctlTimed(fd, kIoctlDiReset, g_outBuf, sizeof g_outBuf, deadline) != 1) {
        return finish({DiscReadStatus::Unreadable, {}});
    }

    std::memset(g_inBuf, 0, sizeof g_inBuf);
    std::memset(g_outBuf, 0, sizeof g_outBuf);
    g_inBuf[0] = static_cast<u32>(kIoctlDiReadId) << 24;
    if (diIoctlTimed(fd, kIoctlDiReadId, g_outBuf, sizeof g_outBuf, deadline) != 1) {
        return finish({DiscReadStatus::Unreadable, {}});
    }
    alignas(32) u8 idBuf[32]{};
    std::memcpy(idBuf, g_outBuf, sizeof idBuf);

    // Zuerst den vollständigen 0x440-Header lesen (liefert den internen Titel), Rückfall auf 0x20-Byte-ID.
    bool gotHeader = false;
    for (s32 cmd : {kIoctlDiUnencRead, kIoctlDiRead}) {
        std::memset(g_inBuf, 0, sizeof g_inBuf);
        std::memset(g_hdrBuf, 0, sizeof g_hdrBuf);
        g_inBuf[0] = static_cast<u32>(cmd) << 24;
        g_inBuf[1] = kHeaderBytes;
        g_inBuf[2] = 0;
        if (diIoctlTimed(fd, cmd, g_hdrBuf, static_cast<s32>(sizeof g_hdrBuf), deadline) == 1) {
            gotHeader = true;
            break;
        }
    }
    OpticalDisc disc = gotHeader ? classifyDisc(g_hdrBuf) : classifyDisc(idBuf);
    if (disc.kind != DiscKind::Wii && disc.kind != DiscKind::Gc && gotHeader) {
        disc = classifyDisc(idBuf);
    }
    if (disc.kind == DiscKind::Wii || disc.kind == DiscKind::Gc) return finish({DiscReadStatus::Ok, std::move(disc)});
    return finish({DiscReadStatus::Unreadable, {}});
}

}  // namespace nl::platform
