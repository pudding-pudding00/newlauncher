#pragma once
// Verbindet CoverCache, Cover-Thread, UI und Renderer (Spec 4.4, 6.4). Nur Haupt-Thread.
// Ablauf pro Frame: beginFrame → Szenen-update (want) → pump → Zeichnen (view/texels) → endFrame des Renderers.
// Texel werden nur in pump() freigegeben; der Renderer hat den Frame davor da schon mit GX_DrawDone beendet.

#include <cstdlib>
#include <string>
#include <unordered_map>

#include "core/cover_cache.hpp"
#include "core/fs.hpp"
#include "gfx/texture_table.hpp"
#include "platform/cover_worker.hpp"
#include "ui/cover_provider.hpp"

namespace nl::app {

class CoverService final : public ui::CoverProvider, public gfx::CoverTextures {
public:
    CoverService(FileSystem& fs, std::string libRoot);
    ~CoverService();

    void beginFrame(u32 frame, float nowMs) { cache_.beginFrame(frame, nowMs); }
    void pump();                    // Ergebnis übernehmen, nächsten Auftrag starten, Marker loggen
    void libraryChanged() { stems_.clear(); }  // Game-Zeiger sind ungültig; der Cache (Stämme) bleibt
    void shutdown();                // vor dem Aushängen der Laufwerke; danach geht pump() normal weiter
    bool idle() const { return cache_.idle() && !worker_.running(); }
    u32 readyCount() const { return cache_.readyCount(); }
    u32 bytesUsed() const { return cache_.bytesUsed(); }

    // ui::CoverProvider
    void want(const Game& game, CoverPriority priority) override;
    ui::CoverView view(const Game& game) const override;

    // gfx::CoverTextures
    const void* texels(gfx::TextureId id) const override;
    u32 generation() const override { return cache_.generation(); }

private:
    struct Memory final : CoverMemory {
        void* alloc(u32 bytes) override;
        void free(void* p) override { std::free(p); }
    };
    struct StemEntry {
        std::string key, stem;
    };
    const std::string& stemOf(const Game& game) const;
    void takeResult();

    FileSystem& fs_;
    std::string root_;
    Memory mem_;
    CoverCache cache_;
    platform::CoverWorker worker_;
    // Stamm je Game-Zeiger, damit want/view pro Frame nicht allokieren. Der Schlüssel wird mitgeprüft:
    // die Detailansicht nutzt dasselbe Game-Objekt für wechselnde Spiele.
    mutable std::unordered_map<const Game*, StemEntry> stems_;
};

}  // namespace nl::app
