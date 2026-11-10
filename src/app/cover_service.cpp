#include "app/cover_service.hpp"

#include <cstring>
#include <malloc.h>

#include "core/cover_paths.hpp"
#include "platform/log.hpp"

namespace nl::app {

void* CoverService::Memory::alloc(u32 bytes) { return memalign(32, bytes); }

CoverService::CoverService(FileSystem& fs, std::string libRoot)
    : fs_(fs), root_(std::move(libRoot)), cache_(kCoverBudgetBytes, mem_) {}

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

void CoverService::want(const Game& game, CoverPriority priority) { cache_.want(stemOf(game), priority); }

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
        if (!worker_.start(fs_, root_, *stem, mem_)) cache_.fail(*stem);
    }
}

void CoverService::takeResult() {
    if (auto r = worker_.take()) {
        if (r->texels != nullptr) {
            if (r->converted) log::line("NL_COVER_CONVERTED %s", r->stem.c_str());
            cache_.complete(r->stem, r->texels, r->bytes);
        } else {
            if (!r->error.empty()) log::line("NL_COVER_FAIL %s %s", r->stem.c_str(), r->error.c_str());
            cache_.fail(r->stem);
        }
    }
}

}  // namespace nl::app
