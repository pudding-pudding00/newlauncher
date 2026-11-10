#pragma once
// Hardware-Messung (Spec 7, M7): Frame-Abstände in Fenstern zu 300 Frames, Cover-Latenzen, Startzeiten.
// Formatiert die NL_PERF-Zeilen, die tools/check_perf.py auswertet. Ohne Heap-Allokation: läuft im
// Render-Loop, und der Selbsttest zählt dort Allokationen.

#include <array>
#include <cstddef>

#include "core/types.hpp"

namespace nl {

class PerfStats {
public:
    static constexpr u32 kWindowFrames = 300;
    static constexpr std::size_t kCoverRing = 1024;

    // Ein Frame ist langsam, wenn sein Abstand zum vorigen > 1,5 × Periode ist (60 Hz: 25 ms, 50 Hz: 30 ms).
    explicit PerfStats(int refreshHz);

    void frame(float intervalMs, bool busy);
    // Der nächste frame()-Abstand zählt nicht: er enthält einen eigenen Log-Flush der Messung (NL_READY,
    // NL_COVERS, NL_COVER_DL_DONE), keine Arbeit des Launchers.
    void pause() { skipNext_ = true; }
    bool windowFull() const { return winFrames_ >= kWindowFrames; }
    // "NL_PERF frames n=… slow=… max_ms=… busy=0|1", danach beginnt ein neues Fenster.
    int takeWindow(char* out, std::size_t cap);

    void coverLoaded(float latencyMs);  // Ring: die letzten kCoverRing Werte zählen
    void setBoot(u32 firstFrameMs, u32 readyMs, u32 usbWaitMs);

    int formatBoot(char* out, std::size_t cap) const;
    int formatSummary(char* out, std::size_t cap, u32 mem1Free, u32 mem2Free, u32 heapFree) const;

    u32 slowFrames() const { return totalSlow_; }
    u32 busySlowFrames() const { return totalBusySlow_; }  // langsam, während Scan/Download/Umwandlung lief

private:
    float percentile(u32 permille) const;  // Nearest-Rank über die Ring-Werte; nicht bei leerem Ring

    int hz_;
    float slowMs_;
    u32 winFrames_ = 0, winSlow_ = 0;
    float winMax_ = 0;
    bool winBusy_ = false;
    u32 totalFrames_ = 0, totalSlow_ = 0, totalBusySlow_ = 0;
    bool skipNext_ = false;
    float totalMax_ = 0;
    std::array<float, kCoverRing> covers_{};
    std::size_t coverCount_ = 0, coverHead_ = 0;
    mutable std::array<float, kCoverRing> scratch_{};
    u32 firstFrameMs_ = 0, readyMs_ = 0, usbWaitMs_ = 0;
};

}  // namespace nl
