#pragma once
// Scan im Hintergrund (Spec 4.3): ein LWP-Thread scannt die Bibliothek, das Ergebnis liegt in einem
// Mutex-geschützten Platz. Die UI-Datenstrukturen gehören weiter nur dem Haupt-Thread.

#include <optional>
#include <string>

#include <ogc/lwp.h>
#include <ogc/mutex.h>

#include "core/scanner.hpp"

namespace nl::platform {

class ScanWorker {
public:
    ScanWorker();
    ~ScanWorker();
    ScanWorker(const ScanWorker&) = delete;
    ScanWorker& operator=(const ScanWorker&) = delete;

    // previous und rules werden kopiert; meta muss bis zum Ende des Scans gültig bleiben. false, wenn schon aktiv.
    bool start(FileSystem& fs, std::string libRoot, const ScanRules& rules, const IndexData* previous, const MetaDb* meta);
    u32 filesSeen() const { return seen_; }
    bool running() const { return running_; }
    std::optional<Result<ScanResult>> take();  // fertiges Ergebnis genau einmal
    // Bricht einen laufenden Scan ab und wartet auf das Thread-Ende; nötig vor dem Aushängen der Laufwerke.
    void cancelAndJoin();

private:
    static void* entry(void* self);
    static bool onProgress(void* self, u32 seen);

    lwp_t thread_ = LWP_THREAD_NULL;
    mutex_t mutex_ = LWP_MUTEX_NULL;
    FileSystem* fs_ = nullptr;
    std::string root_;
    ScanRules rules_;
    IndexData previous_;
    bool hasPrevious_ = false;
    const MetaDb* meta_ = nullptr;
    volatile u32 seen_ = 0;
    volatile bool running_ = false;
    volatile bool cancel_ = false;
    std::optional<Result<ScanResult>> result_;
};

}  // namespace nl::platform
