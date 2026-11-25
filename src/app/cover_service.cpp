#include "app/cover_service.hpp"

#include <cstring>
#include <malloc.h>

#include "core/cover_paths.hpp"
#include "core/tex_file.hpp"
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
    ui::CoverView v{static_cast<gfx::TextureId>(gfx::kCoverTextureBase + *slot), cache_.fadeAlpha(stem)};
    if (const auto box = cache_.contentBox(stem); box && box->x1 > box->x0 && box->y1 > box->y0) {
        v.uv = gfx::UvRect{float(box->x0) / kCoverW, float(box->y0) / kCoverH, float(box->x1) / kCoverW,
                           float(box->y1) / kCoverH};
        v.aspect = float(box->x1 - box->x0) / float(box->y1 - box->y0);
    }
    return v;
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
            // M23: Inhalt ohne transparenten Rand (eingepasstes Querformat), auch aus alten .tex-Dateien.
            const auto box = cmprOpaqueBox(std::span<const u8>(static_cast<const u8*>(r->texels), r->bytes), kCoverW, kCoverH);
            cache_.complete(r->stem, r->texels, r->bytes, box);
        } else {
            if (!r->error.empty()) log::line("NL_COVER_FAIL %s %s", r->stem.c_str(), r->error.c_str());
            cache_.fail(r->stem);
        }
    }
}

}  // namespace nl::app
