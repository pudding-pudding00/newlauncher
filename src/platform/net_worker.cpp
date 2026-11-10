#include "platform/net_worker.hpp"

#include <cstdio>
#include <span>

#include <network.h>
#include <unistd.h>

#include "core/cover_paths.hpp"
#include "core/http.hpp"
#include "platform/http_client.hpp"

namespace nl::platform {

namespace {
constexpr u32 kStackBytes = 64 * 1024;
constexpr u8 kPriority = 40;  // wie Scan und Cover-Thread: unter dem Haupt-Thread
constexpr int kNetInitTries = 3;

std::string parentDir(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string format(const char* fmt, const char* a, const char* b = "", const char* c = "") {
    char buf[160];
    std::snprintf(buf, sizeof buf, fmt, a, b, c);
    return buf;
}

// net_init einmal pro Sitzung; spätere Läufe nutzen die bestehende Verbindung.
bool g_netReady = false;
}  // namespace

NetWorker::NetWorker() { LWP_MutexInit(&mutex_, false); }

NetWorker::~NetWorker() {
    join();
    LWP_MutexDestroy(mutex_);
}

bool NetWorker::start(FileSystem& fs, std::string libRoot, std::vector<CoverJob> jobs,
                      std::vector<std::string> regions, std::string baseUrl) {
    if (running_) return false;
    join();
    fs_ = &fs;
    root_ = std::move(libRoot);
    jobs_ = std::move(jobs);
    regions_ = std::move(regions);
    base_ = std::move(baseUrl);
    done_ = 0;
    total_ = static_cast<u32>(jobs_.size());
    cancel_ = false;
    summary_.reset();
    running_ = true;
    if (LWP_CreateThread(&thread_, &NetWorker::entry, this, nullptr, kStackBytes, kPriority) != 0) {
        running_ = false;
        thread_ = LWP_THREAD_NULL;
        return false;
    }
    return true;
}

void NetWorker::join() {
    cancel_ = true;  // restliche Aufträge auslassen
    if (thread_ != LWP_THREAD_NULL) {
        LWP_JoinThread(thread_, nullptr);
        thread_ = LWP_THREAD_NULL;
    }
}

std::optional<DownloadSummary> NetWorker::take() {
    LWP_MutexLock(mutex_);
    std::optional<DownloadSummary> s = summary_;
    summary_.reset();
    LWP_MutexUnlock(mutex_);
    return s;
}

std::vector<std::string> NetWorker::takeFinishedStems() {
    LWP_MutexLock(mutex_);
    std::vector<std::string> out = std::move(stems_);
    stems_.clear();
    LWP_MutexUnlock(mutex_);
    return out;
}

std::vector<std::string> NetWorker::takeLogLines() {
    LWP_MutexLock(mutex_);
    std::vector<std::string> out = std::move(logLines_);
    logLines_.clear();
    LWP_MutexUnlock(mutex_);
    return out;
}

void NetWorker::log(std::string line) {
    LWP_MutexLock(mutex_);
    logLines_.push_back(std::move(line));
    LWP_MutexUnlock(mutex_);
}

void NetWorker::finished(const std::string& stem) {
    LWP_MutexLock(mutex_);
    stems_.push_back(stem);
    LWP_MutexUnlock(mutex_);
}

void* NetWorker::entry(void* arg) {
    auto* self = static_cast<NetWorker*>(arg);
    const DownloadSummary s = self->run();
    LWP_MutexLock(self->mutex_);
    self->summary_ = s;
    self->running_ = false;
    LWP_MutexUnlock(self->mutex_);
    return nullptr;
}

DownloadSummary NetWorker::run() {
    DownloadSummary sum;
    if (!g_netReady) {
        s32 r = -1;
        for (int i = 0; i < kNetInitTries && r < 0; ++i) {
            r = net_init();
            if (r < 0) usleep(500 * 1000);
        }
        if (r < 0) {
            char code[16];
            std::snprintf(code, sizeof code, "%d", static_cast<int>(r));
            log(format("NL_NET_FAIL %s", code));
            for (const CoverJob& job : jobs_) log(format("NL_COVER_DL %s failed", job.id6.c_str()));
            sum.failed = total_;
            done_ = total_;
            return sum;
        }
        g_netReady = true;
    }
    log("NL_NET ok");

    CoverDownloadIo io;
    io.get = [this](const Url& url, std::vector<u8>& body) {
        return httpGet(url, kMaxCoverDownloadBytes, body, &cancel_);
    };
    io.save = [this](const std::string& stem, std::span<const u8> png) {
        const std::string path = coverSrcPath(root_, stem);
        static_cast<void>(fs_->makeDirs(parentDir(path)));
        return fs_->writeAtomic(path, png).isOk();
    };
    io.cancelled = [this] { return cancel_; };
    io.log = [this](std::string line) { log(std::move(line)); };
    io.finished = [this](const std::string& stem) { finished(stem); };
    io.jobDone = [this] { done_ = done_ + 1; };
    sum = runCoverDownloads(jobs_, regions_, base_, io);
    return sum;
}

}  // namespace nl::platform
