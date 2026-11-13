#pragma once
// Banner-Thread (M18): lädt höchstens ein Banner zugleich, getrennt vom Cover-Thread, damit ein Banner nicht
// hinter einer langen PNG-Umwandlung wartet. Ergebnis Mutex-geschützt wie beim CoverWorker.

#include <optional>
#include <string>

#include <ogc/lwp.h>
#include <ogc/mutex.h>

#include "core/fs.hpp"
#include "core/gc_banner.hpp"

namespace nl::platform {

struct BannerResult {
    std::string key;
    std::optional<GcBanner> gc;  // leer: Fehler, Grund in error
    ErrorCode error = ErrorCode::NotFound;
    std::string context;
};

class BannerWorker {
public:
    BannerWorker();
    ~BannerWorker();
    BannerWorker(const BannerWorker&) = delete;
    BannerWorker& operator=(const BannerWorker&) = delete;

    // GC-Banner aus dem Abbild unter gamePath. false, wenn schon ein Auftrag läuft oder der Thread nicht startet.
    bool startGc(FileSystem& fs, std::string key, std::string gamePath, u8 langIndex, bool shiftJis);
    bool running() const { return running_; }
    std::optional<BannerResult> take();
    void join();

private:
    static void* entry(void* self);

    lwp_t thread_ = LWP_THREAD_NULL;
    mutex_t mutex_ = LWP_MUTEX_NULL;
    FileSystem* fs_ = nullptr;
    std::string key_, path_;
    u8 lang_ = 0;
    bool shiftJis_ = false;
    volatile bool running_ = false;
    std::optional<BannerResult> result_;
};

}  // namespace nl::platform
