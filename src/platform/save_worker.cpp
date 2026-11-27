#include "platform/save_worker.hpp"

#include <ctime>
#include <utility>

namespace nl::platform {

namespace {
// Deflate und SHA-256 über bis zu 16 MiB laufen tief im Aufruf-Stapel; großzügiger als beim Cover-Löschen.
constexpr u32 kStackBytes = 128 * 1024;
constexpr u8 kPriority = 40;  // unter dem Haupt-Thread: die Oberfläche bleibt flüssig

u64 nowUnix() { return static_cast<u64>(std::time(nullptr)); }
}  // namespace

SaveWorker::SaveWorker(FileSystem& fs, std::string dataDir, std::string device)
    : fs_(fs), dataDir_(std::move(dataDir)), device_(device.empty() ? std::string("Wii") : std::move(device)) {
    LWP_MutexInit(&mutex_, false);
}

SaveWorker::~SaveWorker() {
    join();
    LWP_MutexDestroy(mutex_);
}

bool SaveWorker::launch(SaveJobResult::Kind kind) {
    kind_ = kind;
    LWP_MutexLock(mutex_);
    result_.reset();
    LWP_MutexUnlock(mutex_);
    running_ = true;
    if (LWP_CreateThread(&thread_, &SaveWorker::entry, this, nullptr, kStackBytes, kPriority) != 0) {
        running_ = false;
        thread_ = LWP_THREAD_NULL;
        return false;
    }
    return true;
}

bool SaveWorker::startBackup(SaveUnit u, std::string title) {
    if (running_) return false;
    join();
    unit_ = std::move(u);
    title_ = std::move(title);
    zipPath_.clear();
    return launch(SaveJobResult::Kind::Backup);
}

bool SaveWorker::startRestore(SaveUnit u, std::string zipPath, std::string title) {
    if (running_) return false;
    join();
    unit_ = std::move(u);
    title_ = std::move(title);
    zipPath_ = std::move(zipPath);
    return launch(SaveJobResult::Kind::Restore);
}

bool SaveWorker::startScan(SaveUnit u) {
    if (running_) return false;
    join();
    unit_ = std::move(u);
    title_.clear();
    zipPath_.clear();
    return launch(SaveJobResult::Kind::Scan);
}

void SaveWorker::join() {
    if (thread_ == LWP_THREAD_NULL) return;
    LWP_JoinThread(thread_, nullptr);
    thread_ = LWP_THREAD_NULL;
    running_ = false;
}

void* SaveWorker::entry(void* arg) {
    auto* self = static_cast<SaveWorker*>(arg);
    SaveJobResult r;
    r.kind = self->kind_;
    if (self->kind_ == SaveJobResult::Kind::Scan) {
        r.unitId = self->unit_.id;
        const auto files = readUnit(self->fs_, self->unit_);
        r.ok = files.isOk();
        if (files.isOk()) {
            for (const SaveFile& f : files.value()) r.bytes += f.data.size();
            r.files = static_cast<u32>(files.value().size());
        } else {
            r.detail = files.error().context;
        }
    } else if (self->kind_ == SaveJobResult::Kind::Backup) {
        auto b = backupUnit(self->fs_, self->dataDir_, self->unit_, self->title_, self->device_, nowUnix());
        r.ok = b.isOk();
        if (b.isOk())
            r.backup = std::move(b.value());
        else
            r.detail = b.error().context;
    } else {
        auto res = restoreFromZip(self->fs_, self->dataDir_, self->unit_, self->zipPath_, self->title_, self->device_,
                                  nowUnix());
        r.ok = res.isOk();
        if (!res.isOk()) r.detail = res.error().context;
    }
    LWP_MutexLock(self->mutex_);
    self->result_ = std::move(r);
    self->running_ = false;
    LWP_MutexUnlock(self->mutex_);
    return nullptr;
}

std::optional<SaveJobResult> SaveWorker::poll() {
    if (running_) return std::nullopt;
    LWP_MutexLock(mutex_);
    std::optional<SaveJobResult> out = std::move(result_);
    result_.reset();
    LWP_MutexUnlock(mutex_);
    if (out) join();
    return out;
}

}  // namespace nl::platform
