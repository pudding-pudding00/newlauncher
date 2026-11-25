#pragma once
// „Fehlende Cover laden“ (Spec 6.4) als Auftrag an den NetService (M11): je Spiel die Regionen der Reihe nach,
// PNG atomar nach covers_src/<stamm>.png. Fortschritt per volatile Zähler, Rest Mutex-geschützt.
// log::line ist nicht threadsicher: der Thread sammelt seine Marker, der Haupt-Thread loggt sie (takeLogLines).

#include <optional>
#include <string>
#include <vector>

#include <ogc/mutex.h>

#include "core/cover_download.hpp"
#include "core/fs.hpp"
#include "platform/net_service.hpp"

namespace nl::platform {

class NetWorker {
public:
    explicit NetWorker(NetService& service);
    ~NetWorker();
    NetWorker(const NetWorker&) = delete;
    NetWorker& operator=(const NetWorker&) = delete;

    // false, wenn schon ein Lauf aktiv ist oder der NetService den Auftrag nicht annimmt.
    bool start(FileSystem& fs, std::vector<CoverJob> jobs, std::vector<std::string> regions,
               std::string baseUrl);
    u32 done() const { return done_; }
    u32 total() const { return total_; }
    bool running() const { return running_; }
    std::optional<DownloadSummary> take();          // einmal nach dem Ende des Laufs
    std::vector<std::string> takeFinishedStems();  // neu geladene Stämme, für CoverCache::invalidate
    std::vector<std::string> takeLogLines();
    void join();  // eigenen Auftrag abbrechen und auf sein Ende warten; vor dem Aushängen der Laufwerke

private:
    DownloadSummary run(const volatile bool& cancel);
    void log(std::string line);
    void finished(const std::string& stem);
    // M22: <wurzel>/newlauncher/cache/covers/missing.txt je Wurzel fortführen (läuft im Netz-Thread).
    void updateMissingLists(const std::vector<std::string>& tried, const std::vector<std::string>& missed);

    NetService& service_;
    u32 jobId_ = 0;
    mutex_t mutex_ = LWP_MUTEX_NULL;
    FileSystem* fs_ = nullptr;
    std::string base_;
    std::vector<CoverJob> jobs_;
    std::vector<std::string> regions_;
    volatile u32 done_ = 0, total_ = 0;
    volatile bool running_ = false;
    std::optional<DownloadSummary> summary_;
    std::vector<std::string> stems_, logLines_;
};

}  // namespace nl::platform
