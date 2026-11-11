#include "platform/net_worker.hpp"

#include <cstdio>
#include <span>

#include "core/cover_paths.hpp"
#include "core/http.hpp"
#include "platform/http_client.hpp"

namespace nl::platform {

namespace {
std::string parentDir(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string format(const char* fmt, const char* a, const char* b = "", const char* c = "") {
    char buf[160];
    std::snprintf(buf, sizeof buf, fmt, a, b, c);
    return buf;
}
}  // namespace

NetWorker::NetWorker(NetService& service) : service_(service) { LWP_MutexInit(&mutex_, false); }

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
    summary_.reset();
    running_ = true;
    jobId_ = service_.submit([this](const volatile bool& cancel) {
        const DownloadSummary s = run(cancel);
        LWP_MutexLock(mutex_);
        summary_ = s;
        running_ = false;
        LWP_MutexUnlock(mutex_);
    });
    if (jobId_ == 0) {
        running_ = false;
        return false;
    }
    return true;
}

void NetWorker::join() {
    if (jobId_ == 0) return;
    service_.cancelAndWait(jobId_);  // wartend → entfernt, laufend → restliche Spiele auslassen
    jobId_ = 0;
    running_ = false;
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

DownloadSummary NetWorker::run(const volatile bool& cancel) {
    DownloadSummary sum;
    if (!service_.networkUp()) {
        char code[16];
        std::snprintf(code, sizeof code, "%d", static_cast<int>(service_.lastNetCode()));
        log(format("NL_NET_FAIL %s", code));
        for (const CoverJob& job : jobs_) log(format("NL_COVER_DL %s failed", job.id6.c_str()));
        sum.failed = total_;
        done_ = total_;
        return sum;
    }
    log("NL_NET ok");

    CoverDownloadIo io;
    const HttpLog httpLog = [this](std::string line) { log(std::move(line)); };
    io.get = [&cancel, &httpLog](const Url& url, std::vector<u8>& body) {
        return httpGet(url, kMaxCoverDownloadBytes, body, &cancel, httpLog).outcome;
    };
    io.save = [this](const std::string& stem, std::span<const u8> png) {
        const std::string path = coverSrcPath(root_, stem);
        static_cast<void>(fs_->makeDirs(parentDir(path)));
        return fs_->writeAtomic(path, png).isOk();
    };
    io.cancelled = [&cancel] { return cancel; };
    io.log = [this](std::string line) { log(std::move(line)); };
    io.finished = [this](const std::string& stem) { finished(stem); };
    io.jobDone = [this] { done_ = done_ + 1; };
    sum = runCoverDownloads(jobs_, regions_, base_, io);
    return sum;
}

}  // namespace nl::platform
