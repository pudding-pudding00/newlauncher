#include "platform/banner_worker.hpp"

#include "core/gc_disc.hpp"

namespace nl::platform {

namespace {
constexpr u32 kStackBytes = 64 * 1024;
constexpr u8 kPriority = 40;  // wie Cover und Scan: unter dem Haupt-Thread
}  // namespace

BannerWorker::BannerWorker() { LWP_MutexInit(&mutex_, false); }

BannerWorker::~BannerWorker() {
    join();
    LWP_MutexDestroy(mutex_);
}

bool BannerWorker::startGc(FileSystem& fs, std::string key, std::string gamePath, u8 langIndex, bool shiftJis) {
    if (running_) return false;
    join();
    fs_ = &fs;
    key_ = std::move(key);
    path_ = std::move(gamePath);
    lang_ = langIndex;
    shiftJis_ = shiftJis;
    running_ = true;
    if (LWP_CreateThread(&thread_, &BannerWorker::entry, this, nullptr, kStackBytes, kPriority) != 0) {
        running_ = false;
        thread_ = LWP_THREAD_NULL;
        return false;
    }
    return true;
}

void BannerWorker::join() {
    if (thread_ != LWP_THREAD_NULL) {
        LWP_JoinThread(thread_, nullptr);
        thread_ = LWP_THREAD_NULL;
    }
}

void* BannerWorker::entry(void* arg) {
    auto* self = static_cast<BannerWorker*>(arg);
    BannerResult r;
    r.key = self->key_;
    auto loaded = loadGcBanner(*self->fs_, self->path_, self->lang_, self->shiftJis_);
    if (loaded.isOk()) {
        r.gc.emplace(std::move(loaded).value());
    } else {
        r.error = loaded.error().code;
        r.context = loaded.error().context;
    }
    LWP_MutexLock(self->mutex_);
    self->result_.emplace(std::move(r));
    self->running_ = false;
    LWP_MutexUnlock(self->mutex_);
    return nullptr;
}

std::optional<BannerResult> BannerWorker::take() {
    if (running_) return std::nullopt;
    LWP_MutexLock(mutex_);
    std::optional<BannerResult> out = std::move(result_);
    result_.reset();
    LWP_MutexUnlock(mutex_);
    if (out) join();
    return out;
}

}  // namespace nl::platform
