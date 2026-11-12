#include "platform/scan_worker.hpp"

namespace nl::platform {

namespace {
constexpr u32 kStackBytes = 64 * 1024;
constexpr u8 kPriority = 40;  // unter dem Haupt-Thread: Scan läuft vor allem in der VSync-Wartezeit
}  // namespace

ScanWorker::ScanWorker() { LWP_MutexInit(&mutex_, false); }

ScanWorker::~ScanWorker() {
    if (thread_ != LWP_THREAD_NULL) LWP_JoinThread(thread_, nullptr);
    LWP_MutexDestroy(mutex_);
}

bool ScanWorker::start(FileSystem& fs, std::vector<std::string> devices, const ScanRules& rules,
                       std::vector<DeviceIndex> previous, const MetaDb* meta) {
    if (running_) return false;
    if (thread_ != LWP_THREAD_NULL) {  // vorheriger Lauf beendet, aber noch nicht abgeholt
        LWP_JoinThread(thread_, nullptr);
        thread_ = LWP_THREAD_NULL;
    }
    fs_ = &fs;
    devices_ = std::move(devices);
    rules_ = rules;
    previous_ = std::move(previous);
    meta_ = meta;
    seen_ = 0;
    cancel_ = false;
    running_ = true;
    if (LWP_CreateThread(&thread_, &ScanWorker::entry, this, nullptr, kStackBytes, kPriority) != 0) {
        running_ = false;
        thread_ = LWP_THREAD_NULL;
        return false;
    }
    return true;
}

bool ScanWorker::onProgress(void* self, u32 seen) {
    auto* w = static_cast<ScanWorker*>(self);
    w->seen_ = seen;
    return !w->cancel_;
}

void ScanWorker::cancelAndJoin() {
    cancel_ = true;
    if (thread_ != LWP_THREAD_NULL) {
        LWP_JoinThread(thread_, nullptr);
        thread_ = LWP_THREAD_NULL;
    }
    running_ = false;
    LWP_MutexLock(mutex_);
    result_.reset();
    LWP_MutexUnlock(mutex_);
}

void* ScanWorker::entry(void* arg) {
    auto* self = static_cast<ScanWorker*>(arg);
    Result<MultiScan> r =
        scanDevices(*self->fs_, self->devices_, self->rules_, self->previous_, self->meta_, &ScanWorker::onProgress, self);
    LWP_MutexLock(self->mutex_);
    self->result_.emplace(std::move(r));
    self->running_ = false;
    LWP_MutexUnlock(self->mutex_);
    return nullptr;
}

std::optional<Result<MultiScan>> ScanWorker::take() {
    if (running_) return std::nullopt;
    LWP_MutexLock(mutex_);
    std::optional<Result<MultiScan>> out = std::move(result_);
    result_.reset();
    LWP_MutexUnlock(mutex_);
    if (out && thread_ != LWP_THREAD_NULL) {
        LWP_JoinThread(thread_, nullptr);
        thread_ = LWP_THREAD_NULL;
    }
    return out;
}

}  // namespace nl::platform
