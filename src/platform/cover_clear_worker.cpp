#include "platform/cover_clear_worker.hpp"

namespace nl::platform {

namespace {
constexpr u32 kStackBytes = 64 * 1024;
constexpr u8 kPriority = 40;  // unter dem Haupt-Thread: die Oberfläche bleibt flüssig
}  // namespace

CoverClearWorker::CoverClearWorker() { LWP_MutexInit(&mutex_, false); }

CoverClearWorker::~CoverClearWorker() {
    join();
    LWP_MutexDestroy(mutex_);
}

bool CoverClearWorker::start(FileSystem& fs, std::vector<std::string> roots) {
    if (running_) return false;
    join();
    fs_ = &fs;
    roots_ = std::move(roots);
    removed_ = 0;
    cancel_ = false;
    LWP_MutexLock(mutex_);
    result_.reset();
    LWP_MutexUnlock(mutex_);
    running_ = true;
    if (LWP_CreateThread(&thread_, &CoverClearWorker::entry, this, nullptr, kStackBytes, kPriority) != 0) {
        running_ = false;
        thread_ = LWP_THREAD_NULL;
        return false;
    }
    return true;
}

void CoverClearWorker::join() {
    if (thread_ == LWP_THREAD_NULL) return;
    LWP_JoinThread(thread_, nullptr);
    thread_ = LWP_THREAD_NULL;
    running_ = false;
}

void* CoverClearWorker::entry(void* arg) {
    auto* self = static_cast<CoverClearWorker*>(arg);
    CoverClearResult sum;
    for (const std::string& root : self->roots_) {
        const CoverClearResult r = clearCoverCache(
            *self->fs_, root, [self] { self->removed_ = self->removed_ + 1; }, [self] { return self->cancel_; });
        sum.removed += r.removed;
        sum.failed += r.failed;
        if (r.cancelled) {
            sum.cancelled = true;
            break;
        }
    }
    LWP_MutexLock(self->mutex_);
    self->result_ = sum;
    self->running_ = false;
    LWP_MutexUnlock(self->mutex_);
    return nullptr;
}

std::optional<CoverClearResult> CoverClearWorker::take() {
    if (running_) return std::nullopt;
    LWP_MutexLock(mutex_);
    std::optional<CoverClearResult> out = result_;
    result_.reset();
    LWP_MutexUnlock(mutex_);
    if (out) join();
    return out;
}

}  // namespace nl::platform
