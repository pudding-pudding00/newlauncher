#pragma once
// BRLAN-Animationen der Kanal-Banner (M18): pai1 mit Spuren je Pane (RLPA, RLVC, RLVI) bzw. Material (RLTS,
// RLTP, RLMC). Werte sind absolut; applyAnim überschreibt die betroffenen Felder des Ausgangszustands.

#include <span>
#include <string>
#include <vector>

#include "core/banner_scene.hpp"
#include "core/result.hpp"

namespace nl {

struct Key {
    float frame = 0, value = 0, slope = 0;
};

enum class AnimTarget : u8 {
    TransX, TransY, TransZ, RotX, RotY, RotZ, ScaleX, ScaleY, SizeW, SizeH,  // RLPA, Ziel-Index 0–9 wie in der Datei
    VtxColor0 = 10,  // RLVC 0x00–0x0F: Ecke i (0–3), Kanal k (R, G, B, A) → VtxColor0 + 4·i + k
    Alpha = 26,      // RLVC 0x10
    TexTx, TexTy, TexRot, TexSx, TexSy,  // RLTS (nur SRT 0)
    TexPattern,                          // RLTP (nur Textur-Map 0)
    MatR, MatG, MatB, MatA,              // RLMC 0–3
    Visible,                             // RLVI
};

struct Track {
    std::string pane;  // Pane- bzw. Materialname
    bool material = false;
    AnimTarget target = AnimTarget::TransX;
    std::vector<Key> keys;  // nach frame sortiert
    bool step = false;      // RLTP/RLVI: ohne Interpolation
};

struct Anim {
    u16 frames = 0;
    bool loop = false;
    std::vector<std::string> patternTextures;  // RLTP-Werte zeigen hier hinein
    std::vector<Track> tracks;
    u32 skipped = 0;  // unbekannte Spurtypen oder Ziele
};

inline constexpr u32 kAnimMaxTracks = 4096;
inline constexpr u32 kAnimMaxKeys = 4096;  // je Spur
inline constexpr u32 kAnimMaxTotalKeys = 65536;  // je Animation: Spuren dürfen nicht denselben Key-Block teilen und so aufblähen
inline constexpr u32 kAnimNameMax = 255;  // Mustertexturnamen
// Obergrenze für applyAnim je Bild (Vergleiche/Zuweisungen); echte Banner liegen weit darunter.
inline constexpr u32 kAnimMaxWork = 200000;

Result<Anim> parseBrlan(std::span<const u8> file);

// Hermite mit slope zwischen den umgebenden Keys; step: Wert des letzten Keys ≤ frame. Vor dem ersten bzw.
// nach dem letzten Key gilt dessen Wert. Ohne Keys 0.
float evalTrack(const Track& t, float frame);

// Spuren auf den Zustand anwenden. Material-Spuren wirken auf alle Panes mit diesem Material; RLTP-Werte
// werden über patternTextures auf l.textures abgebildet (unbekannter Name → Spur wirkungslos).
void applyAnim(const Layout& l, const Anim& a, float frame, std::vector<PaneState>& state);
// Schleifendurchläufe, die applyAnim je Bild braucht: Pane-Spur = Panes; Material-Spur = Materialien +
// passende Materialien × Panes; Spuren ohne Keys = 0.
u64 animWork(const Layout& l, const Anim& a);

// Abspielfolge: start einmal, danach loop in Schleife; fehlt start, sofort loop. 60 Bilder je Sekunde.
class AnimPlayer {
public:
    void reset(const Anim* start, const Anim* loop);
    void step();  // ein Bild weiter
    const Anim* current() const { return current_; }
    float frame() const { return static_cast<float>(frame_); }

private:
    const Anim* start_ = nullptr;
    const Anim* loop_ = nullptr;
    const Anim* current_ = nullptr;
    u32 frame_ = 0;
};

}  // namespace nl
