#pragma once
// Cover-Thread (Spec 4.3, 6.4): lädt ein Cover als .tex oder wandelt die PNG-Quelle in CMPR um und legt
// die .tex an. Ein Auftrag zur Zeit; das Ergebnis liegt Mutex-geschützt bereit wie beim ScanWorker.

#include <optional>
#include <string>

#include <ogc/lwp.h>
#include <ogc/mutex.h>

#include "core/cover_cache.hpp"
#include "core/fs.hpp"

namespace nl::platform {

struct CoverResult {
    std::string stem;
    void* texels = nullptr;  // aus CoverMemory, kCoverTexBytes groß; nullptr = kein Cover
    u32 bytes = 0;
    bool converted = false;  // PNG umgewandelt (statt .tex gelesen)
    std::string error;       // leer und texels == nullptr: keine Quelle vorhanden
};

class CoverWorker {
public:
    CoverWorker();
    ~CoverWorker();
    CoverWorker(const CoverWorker&) = delete;
    CoverWorker& operator=(const CoverWorker&) = delete;

    // false, wenn schon ein Auftrag läuft oder der Thread nicht startet.
    bool start(FileSystem& fs, std::string libRoot, std::string stem, CoverMemory& mem);
    bool running() const { return running_; }
    std::optional<CoverResult> take();
    void join();  // vor dem Aushängen der Laufwerke

private:
    static void* entry(void* self);
    CoverResult run();

    lwp_t thread_ = LWP_THREAD_NULL;
    mutex_t mutex_ = LWP_MUTEX_NULL;
    FileSystem* fs_ = nullptr;
    CoverMemory* mem_ = nullptr;
    std::string root_, stem_;
    volatile bool running_ = false;
    std::optional<CoverResult> result_;
};

}  // namespace nl::platform
