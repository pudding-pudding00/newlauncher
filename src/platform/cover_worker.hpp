#pragma once
// Cover-Thread (Spec 4.3, 6.4): lädt ein Cover als .tex oder wandelt die PNG-Quelle in CMPR um und legt
// die .tex an. Ein Auftrag zur Zeit; das Ergebnis liegt Mutex-geschützt bereit wie beim ScanWorker.

#include <optional>
#include <string>
#include <vector>

#include <ogc/lwp.h>
#include <ogc/mutex.h>

#include "core/cover_cache.hpp"
#include "core/fs.hpp"

namespace nl::platform {

struct CoverRequest {
    std::string root, stem;
};

struct CoverResult {
    std::string stem;
    void* texels = nullptr;  // aus CoverMemory, kCoverTexBytes groß; nullptr = kein Cover
    u32 bytes = 0;
    std::optional<PixelBox> box;
    bool converted = false;  // PNG umgewandelt (statt .tex gelesen)
    std::string error;       // leer und texels == nullptr: keine Quelle vorhanden
};

struct CoverBatchResult {
    std::vector<CoverResult> results;
    std::vector<std::string> requeue;  // nicht gestartete Stämme (vor PNG-Umwandlung abgebrochen)
};

class CoverWorker {
public:
    CoverWorker();
    ~CoverWorker();
    CoverWorker(const CoverWorker&) = delete;
    CoverWorker& operator=(const CoverWorker&) = delete;

    // false, wenn schon ein Auftrag läuft, die Liste leer ist oder der Thread nicht startet.
    bool start(FileSystem& fs, std::vector<CoverRequest> requests, CoverMemory& mem);
    bool running() const { return running_; }
    std::optional<CoverBatchResult> take();
    void join();  // vor dem Aushängen der Laufwerke

private:
    static void* entry(void* self);
    CoverBatchResult run();
    CoverResult loadOne(const CoverRequest& req, bool allowConvert, bool& skippedConvert);

    lwp_t thread_ = LWP_THREAD_NULL;
    mutex_t mutex_ = LWP_MUTEX_NULL;
    FileSystem* fs_ = nullptr;
    CoverMemory* mem_ = nullptr;
    std::vector<CoverRequest> requests_;
    volatile bool running_ = false;
    std::optional<CoverBatchResult> result_;
};

}  // namespace nl::platform
