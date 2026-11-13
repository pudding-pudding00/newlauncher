#include "platform/cheat_worker.hpp"

#include <array>
#include <cstdio>
#include <span>
#include <string_view>

#include "core/http.hpp"

namespace nl::platform {

CheatWorker::CheatWorker(NetService& service) : service_(service) { LWP_MutexInit(&mutex_, false); }

CheatWorker::~CheatWorker() {
    join();
    LWP_MutexDestroy(mutex_);
}

bool CheatWorker::start(FileSystem& fs, std::string url, std::string id6, std::string destTxt) {
    if (running_) return false;
    join();
    fs_ = &fs;
    url_ = std::move(url);
    id6_ = std::move(id6);
    dest_ = std::move(destTxt);
    LWP_MutexLock(mutex_);
    result_.reset();
    LWP_MutexUnlock(mutex_);
    running_ = true;
    jobId_ = service_.submit([this](const volatile bool& cancel) {
        run(cancel);
        running_ = false;
    });
    if (jobId_ == 0) {
        running_ = false;
        return false;
    }
    return true;
}

void CheatWorker::join() {
    if (jobId_ == 0) return;
    service_.cancelAndWait(jobId_);
    jobId_ = 0;
    running_ = false;
}

std::optional<CheatFetchResult> CheatWorker::takeResult() {
    LWP_MutexLock(mutex_);
    std::optional<CheatFetchResult> out = result_;
    result_.reset();
    LWP_MutexUnlock(mutex_);
    return out;
}

std::vector<std::string> CheatWorker::takeLogLines() {
    LWP_MutexLock(mutex_);
    std::vector<std::string> out = std::move(logLines_);
    logLines_.clear();
    LWP_MutexUnlock(mutex_);
    return out;
}

void CheatWorker::log(std::string line) {
    LWP_MutexLock(mutex_);
    logLines_.push_back(std::move(line));
    LWP_MutexUnlock(mutex_);
}

void CheatWorker::finish(CheatFetchResult r) {
    LWP_MutexLock(mutex_);
    result_ = r;
    LWP_MutexUnlock(mutex_);
}

void CheatWorker::run(const volatile bool& cancel) {
    std::array<u8, 1> probe{};
    if (fs_->read(dest_, 0, probe).isOk()) return finish(CheatFetchResult::Exists);  // nie überschreiben

    const auto url = parseUrl(url_);
    if (!url) {
        log("NL_WARN cheat_url ungültig");
        return finish(CheatFetchResult::NetError);
    }
    if (!service_.networkUp()) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "NL_NET_FAIL %d", static_cast<int>(service_.lastNetCode()));
        log(buf);
        return finish(CheatFetchResult::NetError);
    }
    std::vector<u8> body;
    const HttpResult r = httpGet(*url, kMaxCheatTxtBytes, body, &cancel, [this](std::string line) { log(std::move(line)); });
    if (r.outcome == HttpOutcome::NotFound) return finish(CheatFetchResult::NotFound);
    if (r.outcome != HttpOutcome::Ok) return finish(CheatFetchResult::NetError);

    const std::string_view text(reinterpret_cast<const char*>(body.data()), body.size());
    const CheatFetchResult kind = classifyCheatBody(text, id6_);
    if (kind != CheatFetchResult::Ok) return finish(kind);

    const std::size_t slash = dest_.rfind('/');
    if (slash != std::string::npos) (void)fs_->makeDirs(std::string_view(dest_).substr(0, slash));
    if (!fs_->writeAtomic(dest_, std::span<const u8>(body.data(), body.size())).isOk()) {
        log("NL_WARN cheat_write " + dest_);
        return finish(CheatFetchResult::NetError);
    }
    finish(CheatFetchResult::Ok);
}

}  // namespace nl::platform
