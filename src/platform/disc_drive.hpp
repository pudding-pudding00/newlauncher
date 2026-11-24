#pragma once
// Asynchrones Erkennen der eingelegten optischen Disc über /dev/di (M19, Spec v2 12.1).
// Blockiert nie die Oberfläche: Lesen läuft im Hintergrund-Thread mit 5-s-Zeitlimit.

#include <optional>

#include <ogc/lwp.h>
#include <ogc/mutex.h>

#include "core/disc_id.hpp"
#include "core/types.hpp"

namespace nl::platform {

enum class DiscReadStatus : u8 { Empty, Ok, Unreadable };

struct DiscReadResult {
    DiscReadStatus status = DiscReadStatus::Empty;
    OpticalDisc disc;
};

class DiscDrive {
public:
    DiscDrive();
    ~DiscDrive();
    DiscDrive(const DiscDrive&) = delete;
    DiscDrive& operator=(const DiscDrive&) = delete;

    // [debug] fake_disc (M19-Smoke): umgeht /dev/di und liefert immer diese Disc.
    void setFakeDisc(std::optional<OpticalDisc> fake);

    // Startet einen Hintergrund-Lesevorgang; false, wenn bereits ein Auftrag läuft.
    bool requestRead(bool userInitiated = false);
    bool busy() const { return running_; }

    // Prüft ohne Blockieren auf ein fertiges Ergebnis (0 Allokationen ohne Ergebnis).
    std::optional<DiscReadResult> poll(bool* userInitiated = nullptr);

    // Synchrones Neu-Lesen unmittelbar vor dem Spielstart (Zeitlimit 5 s; nutzt fakeDisc falls gesetzt).
    DiscReadResult readSync();

    void shutdown();

private:
    static void* entry(void* arg);
    DiscReadResult readHardware();
    void join();

    lwp_t thread_ = LWP_THREAD_NULL;
    mutex_t mutex_ = LWP_MUTEX_NULL;
    std::optional<OpticalDisc> fake_;
    bool userInitiated_ = false;
    volatile bool running_ = false;
    volatile bool hasResult_ = false;
    bool resultUserInitiated_ = false;
    DiscReadResult result_;
};

}  // namespace nl::platform
