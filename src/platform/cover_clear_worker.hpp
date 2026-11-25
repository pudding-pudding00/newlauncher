#pragma once
// M24 „Cover-Cache löschen“ im Hintergrund: ein LWP-Thread löscht je Wurzel (clearCoverCache), der Haupt-Thread
// zeigt removed() als Fortschritt und holt das Ergebnis mit take(). Kein Netz, deshalb nicht im NetService.

#include <optional>
#include <string>
#include <vector>

#include <ogc/lwp.h>
#include <ogc/mutex.h>

#include "core/cover_download.hpp"
#include "core/fs.hpp"

namespace nl::platform {

class CoverClearWorker {
public:
    CoverClearWorker();
    ~CoverClearWorker();
    CoverClearWorker(const CoverClearWorker&) = delete;
    CoverClearWorker& operator=(const CoverClearWorker&) = delete;

    bool start(FileSystem& fs, std::vector<std::string> roots);  // false = läuft schon / Thread startet nicht
    bool running() const { return running_; }
    u32 removed() const { return removed_; }
    std::optional<CoverClearResult> take();  // Summe aller Wurzeln, genau einmal nach dem Ende
    void join();                             // auf das Ende warten (vor dem Aushängen der Laufwerke)
    void cancel() { cancel_ = true; }        // M24: vor der nächsten Datei aufhören; danach join()

private:
    static void* entry(void* self);

    lwp_t thread_ = LWP_THREAD_NULL;
    mutex_t mutex_ = LWP_MUTEX_NULL;
    FileSystem* fs_ = nullptr;
    std::vector<std::string> roots_;
    volatile u32 removed_ = 0;
    volatile bool running_ = false;
    volatile bool cancel_ = false;
    std::optional<CoverClearResult> result_;
};

}  // namespace nl::platform
