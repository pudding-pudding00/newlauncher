#include "core/perf_stats.hpp"

#include <algorithm>
#include <cstdio>

namespace nl {

PerfStats::PerfStats(int refreshHz) : hz_(refreshHz > 0 ? refreshHz : 60), slowMs_(1.5f * 1000.0f / static_cast<float>(hz_)) {}

void PerfStats::frame(float intervalMs, bool busy) {
    const bool slow = intervalMs > slowMs_;
    ++winFrames_;
    ++totalFrames_;
    if (slow) {
        ++winSlow_;
        ++totalSlow_;
    }
    winMax_ = std::max(winMax_, intervalMs);
    totalMax_ = std::max(totalMax_, intervalMs);
    winBusy_ = winBusy_ || busy;
}

int PerfStats::takeWindow(char* out, std::size_t cap) {
    const int n = std::snprintf(out, cap, "NL_PERF frames n=%u slow=%u max_ms=%.1f busy=%d", static_cast<unsigned>(winFrames_),
                                static_cast<unsigned>(winSlow_), static_cast<double>(winMax_), winBusy_ ? 1 : 0);
    winFrames_ = winSlow_ = 0;
    winMax_ = 0;
    winBusy_ = false;
    return n;
}

void PerfStats::coverLoaded(float latencyMs) {
    covers_[coverHead_] = latencyMs;
    coverHead_ = (coverHead_ + 1) % kCoverRing;
    if (coverCount_ < kCoverRing) ++coverCount_;
}

void PerfStats::setBoot(u32 firstFrameMs, u32 readyMs, u32 usbWaitMs) {
    firstFrameMs_ = firstFrameMs;
    readyMs_ = readyMs;
    usbWaitMs_ = usbWaitMs;
}

float PerfStats::percentile(u32 permille) const {
    // Nearest-Rank: idx = ceil(p · n) − 1, ganzzahlig gerechnet (0,95 · 100 ist in float nicht exakt 95).
    const std::size_t n = coverCount_;
    std::size_t rank = (static_cast<std::size_t>(permille) * n + 999) / 1000;
    if (rank == 0) rank = 1;
    std::copy(covers_.begin(), covers_.begin() + static_cast<std::ptrdiff_t>(n), scratch_.begin());
    const auto nth = scratch_.begin() + static_cast<std::ptrdiff_t>(rank - 1);
    std::nth_element(scratch_.begin(), nth, scratch_.begin() + static_cast<std::ptrdiff_t>(n));
    return *nth;
}

int PerfStats::formatBoot(char* out, std::size_t cap) const {
    return std::snprintf(out, cap, "NL_PERF boot first_frame_ms=%u ready_ms=%u usb_wait_ms=%u hz=%d",
                         static_cast<unsigned>(firstFrameMs_), static_cast<unsigned>(readyMs_),
                         static_cast<unsigned>(usbWaitMs_), hz_);
}

int PerfStats::formatSummary(char* out, std::size_t cap, u32 mem1Free, u32 mem2Free, u32 heapFree) const {
    float p50 = 0, p95 = 0, pmax = 0;
    if (coverCount_ > 0) {
        p50 = percentile(500);
        p95 = percentile(950);
        pmax = *std::max_element(covers_.begin(), covers_.begin() + static_cast<std::ptrdiff_t>(coverCount_));
    }
    return std::snprintf(out, cap,
                         "NL_PERF summary ready_ms=%u usb_wait_ms=%u hz=%d frames=%u slow=%u max_ms=%.1f cover_n=%u "
                         "cover_p50=%.1f cover_p95=%.1f cover_max=%.1f mem1_free=%u mem2_free=%u heap_free=%u",
                         static_cast<unsigned>(readyMs_), static_cast<unsigned>(usbWaitMs_), hz_,
                         static_cast<unsigned>(totalFrames_), static_cast<unsigned>(totalSlow_),
                         static_cast<double>(totalMax_), static_cast<unsigned>(coverCount_), static_cast<double>(p50),
                         static_cast<double>(p95), static_cast<double>(pmax), static_cast<unsigned>(mem1Free),
                         static_cast<unsigned>(mem2Free), static_cast<unsigned>(heapFree));
}

}  // namespace nl
