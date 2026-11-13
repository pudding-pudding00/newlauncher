#pragma once
// Verbindet Banner-Thread, Detailansicht und Renderer (M18). Nur Haupt-Thread. Ein Banner zugleich:
// wünscht die Szene ein anderes Spiel, wird das alte Banner freigegeben; ein noch laufender Auftrag für das
// alte Spiel wird zu Ende gelesen und verworfen. Texel werden nur in pump() freigegeben (wie bei den Covern).

#include <string>
#include <vector>

#include "core/fs.hpp"
#include "gfx/renderer.hpp"
#include "platform/banner_worker.hpp"
#include "ui/banner_provider.hpp"

namespace nl::app {

class BannerService final : public ui::BannerProvider {
public:
    BannerService(FileSystem& fs, gfx::Renderer& renderer) : fs_(fs), renderer_(renderer) {}
    ~BannerService();
    // roots[d] = Gerätewurzel ("sd:") des Bibliotheksgeräts d, Index wie Game::device.
    void setRoots(std::vector<std::string> roots) { roots_ = std::move(roots); }
    void setGerman(bool german) { german_ = german; }
    void pump();      // Ergebnis übernehmen, nächsten Auftrag starten
    void release();   // Banner freigeben (Bibliothek neu, Szene verlassen)
    void shutdown();  // vor dem Aushängen der Laufwerke

    // ui::BannerProvider
    void want(const Game& game) override;
    ui::BannerView view(const Game& game) const override;

private:
    void freeTexels();

    FileSystem& fs_;
    gfx::Renderer& renderer_;
    platform::BannerWorker worker_;
    std::vector<std::string> roots_;
    bool german_ = false;
    std::string key_, path_;   // gewünschtes Spiel
    bool requested_ = false;   // Auftrag für key_ gestartet
    void* texels_ = nullptr;   // RGB5A3 96×32 für key_, 32-Byte-ausgerichtet
};

}  // namespace nl::app
