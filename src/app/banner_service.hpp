#pragma once
// Verbindet Banner-Thread, Detailansicht und Renderer (M18). Nur Haupt-Thread. Ein Banner zugleich:
// wünscht die Szene ein anderes Spiel, wird das alte Banner freigegeben; ein noch laufender Auftrag für das
// alte Spiel wird zu Ende gelesen und verworfen. Texel werden nur in pump() freigegeben (wie bei den Covern).

#include <string>
#include <vector>

#include <optional>

#include "core/banner_budget.hpp"
#include "core/banner_scene.hpp"
#include "core/banner_sound.hpp"
#include "core/brlan.hpp"
#include "core/fs.hpp"
#include "core/vc_map.hpp"
#include "gfx/renderer.hpp"
#include "platform/audio.hpp"
#include "platform/banner_tex.hpp"
#include "platform/banner_worker.hpp"
#include "platform/music.hpp"
#include "ui/banner_provider.hpp"

namespace nl::app {

class BannerService final : public ui::BannerProvider {
public:
    BannerService(FileSystem& fs, gfx::Renderer& renderer) : fs_(fs), renderer_(renderer) {}
    ~BannerService();
    // roots[d] = Gerätewurzel ("sd:") des Bibliotheksgeräts d, Index wie Game::device.
    void setRoots(std::vector<std::string> roots) { roots_ = std::move(roots); }
    void setGerman(bool german) { german_ = german; }
    // [debug] banner_file: dieses Kanal-content0 statt des GC-Banners laden (Smoke, M18).
    void setDebugFile(std::string path) { debugFile_ = std::move(path); }
    // Banner-Ton (Task 7): ohne Audio bleibt das Banner stumm. Music wird währenddessen gedrückt.
    void setAudio(platform::Audio* audio, platform::Music* music) {
        audio_ = audio;
        music_ = music;
    }
    // Kanäle: VC-Zuordnung Spiel → Kanal-Titel (v1); NAND-content0 des Kanals liefert das Banner.
    void setVcMap(const VcMap* map) { vcMap_ = map; }
    void setLogSounds(bool on) { logSounds_ = on; }  // [debug] log_sounds: NL_SOUND banner
    void setEnabled(bool on);  // [ui] banners; aus → sofort freigeben
    // Ein Auftrag startet erst nach so langem Fokus; ein Wechsel davor lädt nichts.
    static constexpr u32 kFocusDelayMs = 1000;
    void pump();      // Ergebnis übernehmen, nächsten Auftrag starten
    void release();   // Banner freigeben (Bibliothek neu, Szene verlassen); ohne Banner ein No-op
    void shutdown();  // vor dem Aushängen der Laufwerke

    // ui::BannerProvider
    void want(const Game& game) override;
    ui::BannerView view(const Game& game) const override;

private:
    void freeTexels();
    void freeFrame();
    void takeChannel(ChannelBanner& banner);
    void animate();  // ein Bild der Animation weiter, Quads neu
    void updateSound();
    void stopSound();  // Ton aus, Puffer frei, Musik wieder laut

    FileSystem& fs_;
    gfx::Renderer& renderer_;
    platform::BannerWorker worker_;
    std::vector<std::string> roots_;
    bool german_ = false;
    std::string key_, path_;   // gewünschtes Spiel
    bool requested_ = false;   // Auftrag für key_ gestartet (oder nichts zu laden)
    bool gc_ = false;          // key_ ist ein GC-Spiel
    std::optional<u64> titleId_;  // Kanal-Titel aus der VC-Zuordnung
    const VcMap* vcMap_ = nullptr;
    bool enabled_ = true;
    bool logSounds_ = false;
    void* texels_ = nullptr;   // RGB5A3 96×32 für key_, 32-Byte-ausgerichtet
    std::string debugFile_;
    platform::BannerTextures textures_;  // Kanal-Banner für key_
    Layout layout_;
    std::vector<Quad> quads_;
    std::optional<Anim> start_, loop_;
    AnimPlayer player_;
    ui::BannerFrame frame_{};
    bool haveFrame_ = false;
    platform::Audio* audio_ = nullptr;
    platform::Music* music_ = nullptr;
    BannerSoundGate gate_;
    u64 focusTicks_ = 0;  // gettime() beim Fokuswechsel
};

}  // namespace nl::app
