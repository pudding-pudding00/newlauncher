#pragma once
// Verbindet CoverCache, Cover-Thread, UI und Renderer (Spec 4.4, 6.4). Nur Haupt-Thread.
// Ablauf pro Frame: beginFrame → Szenen-update (want) → pump → Zeichnen (view/texels) → endFrame des Renderers.
// Texel werden nur in pump() freigegeben; der Renderer hat den Frame davor da schon mit GX_DrawDone beendet.

#include <cstdlib>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/cover_cache.hpp"
#include "core/fs.hpp"
#include "core/perf_stats.hpp"
#include "gfx/texture_table.hpp"
#include "platform/cover_worker.hpp"
#include "ui/cover_provider.hpp"

namespace nl::app {

class CoverService final : public ui::CoverProvider, public gfx::CoverTextures {
public:
    // roots[d] = Cover-Wurzel des Geräts d (coverRoot), Index wie Game::device.
    CoverService(FileSystem& fs, std::vector<std::string> roots);
    void setRoots(std::vector<std::string> roots) { roots_ = std::move(roots); }
    ~CoverService();

    void beginFrame(u32 frame, float nowMs) { cache_.beginFrame(frame, nowMs); }
    void pump();                    // Ergebnis übernehmen, nächsten Auftrag starten, Marker loggen
    void libraryChanged() { stems_.clear(); }  // Game-Zeiger sind ungültig; der Cache (Stämme) bleibt
    void invalidate(std::string_view stem) { cache_.invalidate(stem); }  // neue Quelle geladen: Missing erneut versuchen
    void shutdown();                // vor dem Aushängen der Laufwerke; danach geht pump() normal weiter
    void clearAll() {               // M23 „Cover-Cache löschen“: Thread anhalten, alle Texturen verwerfen
        shutdown();
        cache_.clear();
    }
    bool idle() const { return cache_.idle() && !worker_.running(); }
    u32 readyCount() const { return cache_.readyCount(); }
    u32 bytesUsed() const { return cache_.bytesUsed(); }
    void setPerf(PerfStats* perf) { perf_ = perf; }  // nullptr: keine Messung
    // Perf: der Thread arbeitet und hat zuletzt eine PNG umgewandelt. Eine Umwandlung belegt die CPU spürbar,
    // ein .tex-Ladevorgang kaum; ob der laufende Auftrag umwandelt, steht erst in seinem Ergebnis.
    bool converting() const { return worker_.running() && lastConverted_; }

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
    std::vector<std::string> roots_;
    std::unordered_map<std::string, u8> deviceOfStem_;  // aus want(); Stämme sind nach dem Zusammenführen eindeutig
    Memory mem_;
    CoverCache cache_;
    platform::CoverWorker worker_;
    PerfStats* perf_ = nullptr;
    bool lastConverted_ = false;
    // Stamm je Game-Zeiger, damit want/view pro Frame nicht allokieren. Der Schlüssel wird mitgeprüft:
    // die Detailansicht nutzt dasselbe Game-Objekt für wechselnde Spiele.
    mutable std::unordered_map<const Game*, StemEntry> stems_;
};

}  // namespace nl::app
