#pragma once
// Banner-Thread (M18): lädt höchstens ein Banner zugleich, getrennt vom Cover-Thread, damit ein Banner nicht
// hinter einer langen PNG-Umwandlung wartet. Ergebnis Mutex-geschützt wie beim CoverWorker.

#include <optional>
#include <string>

#include <ogc/lwp.h>
#include <ogc/mutex.h>

#include "core/channel_banner.hpp"
#include "core/fs.hpp"
#include "core/gc_banner.hpp"

namespace nl::platform {

struct BannerResult {
    std::string key;
    std::optional<GcBanner> gc;            // GC-Auftrag; leer: Fehler, Grund in error
    std::optional<ChannelBanner> channel;  // Kanal-Auftrag (M18 Task 5); leer: Fehler
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
    // Kanal-Banner aus Content 0 im NAND (titleId) oder, für den Smoke, aus einer Datei (filePath nicht leer).
    bool startChannel(FileSystem& fs, std::string key, u64 titleId, std::string filePath);
    // Wii-Spiel: opening.bnr aus dem Abbild (.iso/.wbfs), Ergebnis wie ein Kanal-Banner (Task 9).
    bool startWii(FileSystem& fs, std::string key, std::string gamePath);
    bool running() const { return running_; }
    std::optional<BannerResult> take();
    void join();

private:
    static void* entry(void* self);
    bool launch();
    void runChannel(BannerResult& r);

    lwp_t thread_ = LWP_THREAD_NULL;
    mutex_t mutex_ = LWP_MUTEX_NULL;
    FileSystem* fs_ = nullptr;
    std::string key_, path_;
    bool channel_ = false;
    bool wii_ = false;
    u64 titleId_ = 0;
    u8 lang_ = 0;
    bool shiftJis_ = false;
    volatile bool running_ = false;
    std::optional<BannerResult> result_;
};

}  // namespace nl::platform
