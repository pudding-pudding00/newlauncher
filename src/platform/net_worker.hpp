#pragma once
// Netz-Thread für „Fehlende Cover laden“ (Spec 6.4): net_init, dann je Auftrag die Regionen der Reihe nach,
// PNG atomar nach covers_src/<stamm>.png. Fortschritt per volatile Zähler, Rest Mutex-geschützt.
// log::line ist nicht threadsicher: der Thread sammelt seine Marker, der Haupt-Thread loggt sie (takeLogLines).

#include <optional>
#include <string>
#include <vector>

#include <ogc/lwp.h>
#include <ogc/mutex.h>

#include "core/cover_download.hpp"
#include "core/fs.hpp"

namespace nl::platform {

class NetWorker {
public:
    NetWorker();
    ~NetWorker();
    NetWorker(const NetWorker&) = delete;
    NetWorker& operator=(const NetWorker&) = delete;

    // false, wenn schon ein Lauf aktiv ist oder der Thread nicht startet.
    bool start(FileSystem& fs, std::string libRoot, std::vector<CoverJob> jobs, std::vector<std::string> regions,
               std::string baseUrl);
    u32 done() const { return done_; }
    u32 total() const { return total_; }
    bool running() const { return running_; }
    std::optional<DownloadSummary> take();          // einmal nach dem Ende des Laufs
    std::vector<std::string> takeFinishedStems();  // neu geladene Stämme, für CoverCache::invalidate
    std::vector<std::string> takeLogLines();
    void join();  // vor dem Aushängen der Laufwerke; wartet höchstens auf den laufenden Auftrag

private:
    static void* entry(void* self);
    DownloadSummary run();
    void log(std::string line);
    void finished(const std::string& stem);

    lwp_t thread_ = LWP_THREAD_NULL;
    mutex_t mutex_ = LWP_MUTEX_NULL;
    FileSystem* fs_ = nullptr;
    std::string root_, base_;
    std::vector<CoverJob> jobs_;
    std::vector<std::string> regions_;
    volatile u32 done_ = 0, total_ = 0;
    volatile bool running_ = false;
    volatile bool cancel_ = false;
    std::optional<DownloadSummary> summary_;
    std::vector<std::string> stems_, logLines_;
};

}  // namespace nl::platform
