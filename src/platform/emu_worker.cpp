#include "platform/emu_worker.hpp"

#include <cstdio>

#include "core/http.hpp"
#include "platform/http_client.hpp"
#include "platform/sd_space.hpp"

namespace nl::platform {

EmuWorker::EmuWorker(NetService& service) : service_(service) { LWP_MutexInit(&mutex_, false); }

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
    running_ = true;
    jobId_ = service_.submit([this](const volatile bool& cancel) {
        if (job_ == Job::List)
            runList(cancel);
        else
            runInstall(cancel);
        running_ = false;
    });
    if (jobId_ == 0) {
        running_ = false;
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

void EmuWorker::cancel() {
    if (jobId_ == 0) return;
    service_.cancel(jobId_);
    if (!service_.active(jobId_)) {  // wartete noch und ist jetzt entfernt: kein Lauf, der running_ zurücksetzt
        jobId_ = 0;
        running_ = false;
    }
}

void EmuWorker::join() {
    if (jobId_ == 0) return;
    service_.cancelAndWait(jobId_);
    jobId_ = 0;
    running_ = false;
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

bool EmuWorker::network() {
    if (service_.networkUp()) return true;
    char buf[32];
    std::snprintf(buf, sizeof buf, "NL_NET_FAIL %d", static_cast<int>(service_.lastNetCode()));
    log(buf);
    return false;
}

void EmuWorker::runList(const volatile bool& cancel) {
    const auto url = parseUrl(oscListUrl(repo_));
    std::vector<u8> body;
    if (!url || !network() || httpGet(*url, kMaxOscListBytes, body, &cancel) != HttpOutcome::Ok) {
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

void EmuWorker::runInstall(const volatile bool& cancel) {
    const bool net = network();
    EmuInstallIo io;
    io.download = [this, net, &cancel](const std::string& url, const std::string& dest, u32 maxBytes) {
        const auto u = parseUrl(url);
        if (!net || !u) return HttpOutcome::Failed;
        return httpDownload(*u, dest, maxBytes, &cancel, &done_);
    };
    io.freeBytes = [] { return sdFreeBytes(); };
    io.cancelled = [&cancel] { return cancel; };
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
        if (cancel)
            r = EmuInstallResult{EmuInstallError::Cancelled, "queue"};
        else
            r = installEmulator(*fs_, io, repo_, apps_[i]);
        LWP_MutexLock(mutex_);
        results_.emplace_back(apps_[i].name, std::move(r));
        LWP_MutexUnlock(mutex_);
    }
}

}  // namespace nl::platform
