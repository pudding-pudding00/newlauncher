#include "platform/net_worker.hpp"

#include <algorithm>
#include <cstdio>
#include <span>

#include <ogc/lwp_watchdog.h>

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

bool NetWorker::start(FileSystem& fs, std::vector<CoverJob> jobs,
                      std::vector<std::string> regions, std::string baseUrl) {
    if (running_) return false;
    join();
    fs_ = &fs;
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

void NetWorker::cancel() {
    if (jobId_ != 0) service_.cancel(jobId_);
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
        sum.lastError = NetError::Other;
        done_ = total_;
        return sum;
    }
    log("NL_NET ok");

    CoverDownloadIo io;
    const HttpLog httpLog = [this](std::string line) { log(std::move(line)); };
    NetError lastError = NetError::None;
    const bool perf = netPerf();
    io.get = [this, perf, &cancel, &httpLog, &lastError](const Url& url, std::vector<u8>& body) {
        const u64 t0 = gettime();
        const HttpResult r = httpGet(url, kMaxCoverDownloadBytes, body, &cancel, httpLog);
        if (r.outcome == HttpOutcome::Failed) lastError = r.error;
        if (perf) {  // M23: wohin geht die Zeit je Cover? (Handshake, Übertragung, Größe)
            char line[112];
            std::snprintf(line, sizeof line, "NL_PERF cover get status=%u ms=%u hs=%u bytes=%u reuse=%d",
                          static_cast<unsigned>(r.status), static_cast<unsigned>(ticks_to_millisecs(diff_ticks(t0, gettime()))),
                          static_cast<unsigned>(lastHandshakeMs()), static_cast<unsigned>(body.size()),
                          lastRequestReused() ? 1 : 0);
            log(line);
        }
        return r.outcome;
    };
    io.save = [this](const std::string& stem, std::span<const u8> png) {
        // Ziel ist das Gerät des Spiels (CoverJob::root); Stämme sind nach dem Zusammenführen eindeutig.
        const CoverJob* job = nullptr;
        for (const CoverJob& j : jobs_)
            if (j.stem == stem) job = &j;
        if (job == nullptr) return false;
        const std::string path = coverSrcPath(job->root, stem);
        const u64 t0 = gettime();
        static_cast<void>(fs_->makeDirs(parentDir(path)));
        const bool ok = fs_->writeAtomic(path, png).isOk();
        if (netPerf()) {
            char line[64];
            std::snprintf(line, sizeof line, "NL_PERF cover save ms=%u",
                          static_cast<unsigned>(ticks_to_millisecs(diff_ticks(t0, gettime()))));
            log(line);
        }
        return ok;
    };
    io.cancelled = [&cancel] { return cancel; };
    io.log = [this](std::string line) { log(std::move(line)); };
    std::vector<std::string> tried, missed;  // M22: für die Fehlliste (nur Aufträge mit Antwort zählen als versucht)
    io.finished = [this, &tried](const std::string& stem) {
        tried.push_back(stem);
        finished(stem);
    };
    io.missed = [&tried, &missed](const std::string& stem) {
        tried.push_back(stem);
        missed.push_back(stem);
    };
    io.jobDone = [this] { done_ = done_ + 1; };
    sum = runCoverDownloads(jobs_, regions_, base_, io);
    if (sum.failed > 0) sum.lastError = lastError;
    updateMissingLists(tried, missed);
    return sum;
}

void NetWorker::updateMissingLists(const std::vector<std::string>& tried, const std::vector<std::string>& missed) {
    constexpr std::size_t kMaxMissingListBytes = 256u * 1024;
    std::vector<std::string> roots;
    for (const CoverJob& j : jobs_)
        if (std::find(roots.begin(), roots.end(), j.root) == roots.end()) roots.push_back(j.root);
    for (const std::string& root : roots) {
        std::vector<std::string> triedHere, missedHere;
        for (const CoverJob& j : jobs_) {
            if (j.root != root) continue;
            if (std::find(tried.begin(), tried.end(), j.stem) != tried.end()) triedHere.push_back(j.stem);
            if (std::find(missed.begin(), missed.end(), j.stem) != missed.end()) missedHere.push_back(j.stem);
        }
        const std::string path = missingListPath(root);
        std::string oldText;
        if (auto data = fs_->readAll(path, kMaxMissingListBytes); data.isOk())
            oldText.assign(reinterpret_cast<const char*>(data.value().data()), data.value().size());
        const std::string text = missingListText(updateMissingList(parseMissingList(oldText), triedHere, missedHere));
        if (text == oldText) continue;
        static_cast<void>(fs_->makeDirs(parentDir(path)));
        const auto bytes = std::span<const u8>(reinterpret_cast<const u8*>(text.data()), text.size());
        const bool ok = fs_->writeAtomic(path, bytes).isOk();
        char count[16];
        std::snprintf(count, sizeof count, "%u", static_cast<unsigned>(parseMissingList(text).size()));
        log(format("NL_COVER_MISSING_LIST %s entries=%s%s", root.c_str(), count, ok ? "" : " write_failed"));
    }
}

}  // namespace nl::platform
