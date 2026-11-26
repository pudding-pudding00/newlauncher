#pragma once
// Abgleich-Szene (M27) vor dem Spielstart: Titel, Text je Phase, drehende Punkte und „B: Ohne Abgleich spielen“.
// B schließt und meldet CloudSkip; main startet dann ohne Abgleich. Das Ergebnis schließt sie über main.

#include <string>

#include "ui/scene.hpp"

namespace nl::ui {

enum class CloudPhase : u8 { Checking, Resolving };

class CloudSyncScene final : public Scene {
public:
    void open(std::string title);  // Phase Checking
    void setPhase(CloudPhase p) { phase_ = p; }
    CloudPhase phase() const { return phase_; }

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }

private:
    std::string title_;
    CloudPhase phase_ = CloudPhase::Checking;
    float elapsedMs_ = 0.0f;
};

}  // namespace nl::ui
