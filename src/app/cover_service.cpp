#include "app/cover_service.hpp"

#include <cstring>
#include <malloc.h>

#include "core/cover_paths.hpp"
#include "platform/log.hpp"

namespace nl::app {

void* CoverService::Memory::alloc(u32 bytes) { return memalign(32, bytes); }

CoverService::CoverService(FileSystem& fs, std::vector<std::string> roots)
    : fs_(fs), roots_(std::move(roots)), cache_(kCoverBudgetBytes, mem_) {}

CoverService::~CoverService() { shutdown(); }

void CoverService::shutdown() {
    worker_.join();
    takeResult();  // ein fertiges Ergebnis landet im Cache, sonst hinge der Eintrag als „in Arbeit“
}

const std::string& CoverService::stemOf(const Game& game) const {
    const char* key = game.key != nullptr ? game.key : "";
    StemEntry& e = stems_[&game];
    if (e.stem.empty() || e.key != key) {
        e.key = key;
        e.stem = coverStem(game);
    }
    return e.stem;
}

void CoverService::want(const Game& game, CoverPriority priority) {
    if (game.device >= roots_.size()) return;
    const std::string& stem = stemOf(game);
    if (cache_.slotOf(stem)) return;  // fertig: kein Map-Zugriff je Frame
    deviceOfStem_.insert_or_assign(stem, game.device);
    cache_.want(stem, priority);
}

ui::CoverView CoverService::view(const Game& game) const {
    const std::string& stem = stemOf(game);
    const auto slot = cache_.slotOf(stem);
    if (!slot) return {};
    return ui::CoverView{static_cast<gfx::TextureId>(gfx::kCoverTextureBase + *slot), cache_.fadeAlpha(stem)};
}

const void* CoverService::texels(gfx::TextureId id) const {
    if (id < gfx::kCoverTextureBase) return nullptr;
    return cache_.texelsOfSlot(static_cast<u16>(id - gfx::kCoverTextureBase));
}

void CoverService::pump() {
    takeResult();
    if (worker_.running()) return;
    if (auto stem = cache_.nextRequest()) {
        const auto dev = deviceOfStem_.find(*stem);
        const u8 d = dev != deviceOfStem_.end() ? dev->second : 0;
        if (d >= roots_.size() || !worker_.start(fs_, roots_[d], *stem, mem_)) cache_.fail(*stem);
    }
}

void CoverService::takeResult() {
    if (auto r = worker_.take()) {
        lastConverted_ = r->converted;
        if (r->texels != nullptr) {
            if (r->converted) log::line("NL_COVER_CONVERTED %s", r->stem.c_str());
            // Latenz nur für gelesene .tex: eine Umwandlung ist einmalig und hat ihr eigenes Budget.
            if (perf_ != nullptr && !r->converted) {
                if (const auto at = cache_.requestedAtMs(r->stem)) perf_->coverLoaded(cache_.nowMs() - *at);
            }
            cache_.complete(r->stem, r->texels, r->bytes);
        } else {
            if (!r->error.empty()) log::line("NL_COVER_FAIL %s %s", r->stem.c_str(), r->error.c_str());
            cache_.fail(r->stem);
        }
    }
}

}  // namespace nl::app
