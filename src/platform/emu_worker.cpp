#include "platform/emu_worker.hpp"

#include <cstdio>

#include "core/http.hpp"
#include "platform/http_client.hpp"
#include "platform/sd_space.hpp"

namespace nl::platform {

namespace {
constexpr u32 kStackBytes = 64 * 1024;
constexpr u8 kPriority = 40;  // wie NetWorker: unter dem Haupt-Thread
}  // namespace

EmuWorker::EmuWorker() { LWP_MutexInit(&mutex_, false); }

EmuWorker::~EmuWorker() {
    join();
    LWP_MutexDestroy(mutex_);
}

bool EmuWorker::launch(Job job) {
    job_ = job;
    done_ = 0;
    total_ = 0;
    appIndex_ = 0;
    step_ = static_cast<u8>(EmuStep::Download);
    cancel_ = false;
    running_ = true;
    if (LWP_CreateThread(&thread_, &EmuWorker::entry, this, nullptr, kStackBytes, kPriority) != 0) {
        running_ = false;
        thread_ = LWP_THREAD_NULL;
        return false;
    }
    return true;
}

bool EmuWorker::startFetchList(std::string repo) {
    if (running_) return false;
    join();
    repo_ = std::move(repo);
    listFailed_ = false;
    LWP_MutexLock(mutex_);
    list_.reset();
    LWP_MutexUnlock(mutex_);
    appCount_ = 0;
    return launch(Job::List);
}

bool EmuWorker::startInstall(FileSystem& fs, std::string repo, std::vector<OscApp> apps) {
    if (running_ || apps.empty()) return false;
    join();
    fs_ = &fs;
    repo_ = std::move(repo);
    apps_ = std::move(apps);
    appCount_ = static_cast<u32>(apps_.size());
    return launch(Job::Install);
}

void EmuWorker::join() {
    cancel_ = true;
    if (thread_ != LWP_THREAD_NULL) {
        LWP_JoinThread(thread_, nullptr);
        thread_ = LWP_THREAD_NULL;
    }
}

std::optional<std::vector<OscApp>> EmuWorker::takeList() {
    LWP_MutexLock(mutex_);
    std::optional<std::vector<OscApp>> out = std::move(list_);
    list_.reset();
    LWP_MutexUnlock(mutex_);
    return out;
}

std::vector<std::pair<std::string, EmuInstallResult>> EmuWorker::takeResults() {
    LWP_MutexLock(mutex_);
    auto out = std::move(results_);
    results_.clear();
    LWP_MutexUnlock(mutex_);
    return out;
}

std::vector<std::string> EmuWorker::takeLogLines() {
    LWP_MutexLock(mutex_);
    std::vector<std::string> out = std::move(logLines_);
    logLines_.clear();
    LWP_MutexUnlock(mutex_);
    return out;
}

std::string EmuWorker::currentName() {
    LWP_MutexLock(mutex_);
    std::string out = current_;
    LWP_MutexUnlock(mutex_);
    return out;
}

void EmuWorker::log(std::string line) {
    LWP_MutexLock(mutex_);
    logLines_.push_back(std::move(line));
    LWP_MutexUnlock(mutex_);
}

void* EmuWorker::entry(void* arg) {
    auto* self = static_cast<EmuWorker*>(arg);
    if (self->job_ == Job::List)
        self->runList();
    else
        self->runInstall();
    self->running_ = false;
    return nullptr;
}

bool EmuWorker::network() {
    s32 code = 0;
    if (ensureNetwork(code)) return true;
    char buf[32];
    std::snprintf(buf, sizeof buf, "NL_NET_FAIL %d", static_cast<int>(code));
    log(buf);
    return false;
}

void EmuWorker::runList() {
    const auto url = parseUrl(oscListUrl(repo_));
    std::vector<u8> body;
    if (!url || !network() || httpGet(*url, kMaxOscListBytes, body, &cancel_) != HttpOutcome::Ok) {
        listFailed_ = true;
        log("NL_EMU_LIST fail");
        return;
    }
    auto apps = parseOscList(std::string_view(reinterpret_cast<const char*>(body.data()), body.size()));
    if (apps.empty()) {
        listFailed_ = true;
        log("NL_EMU_LIST fail");
        return;
    }
    log("NL_EMU_LIST ok n=" + std::to_string(apps.size()));
    LWP_MutexLock(mutex_);
    list_ = std::move(apps);
    LWP_MutexUnlock(mutex_);
}

void EmuWorker::runInstall() {
    const bool net = network();
    EmuInstallIo io;
    io.download = [this, net](const std::string& url, const std::string& dest, u32 maxBytes) {
        const auto u = parseUrl(url);
        if (!net || !u) return HttpOutcome::Failed;
        return httpDownload(*u, dest, maxBytes, &cancel_, &done_);
    };
    io.freeBytes = [] { return sdFreeBytes(); };
    io.cancelled = [this] { return cancel_; };
    io.log = [this](std::string line) { log(std::move(line)); };
    io.progress = [this](EmuStep s, u32 done, u32 total) {
        step_ = static_cast<u8>(s);
        done_ = done;
        total_ = total;
    };
    for (u32 i = 0; i < apps_.size(); ++i) {
        appIndex_ = i;
        LWP_MutexLock(mutex_);
        current_ = apps_[i].name;
        LWP_MutexUnlock(mutex_);
        EmuInstallResult r;
        if (cancel_)
            r = EmuInstallResult{EmuInstallError::Cancelled, "queue"};
        else
            r = installEmulator(*fs_, io, repo_, apps_[i]);
        LWP_MutexLock(mutex_);
        results_.emplace_back(apps_[i].name, std::move(r));
        LWP_MutexUnlock(mutex_);
    }
}

}  // namespace nl::platform
