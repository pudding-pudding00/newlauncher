#pragma once
// HOME-Menü (Spec 3.3/6.8): Overlay mit Homebrew Channel, Wii-Menü, Einstellungen, Ausschalten.
// „Einstellungen" öffnet die Einstellungsszene; der Akkustand folgt in M6.

#include "ui/scene.hpp"

namespace nl::ui {

class HomeMenu final : public Scene {
public:
    static constexpr int kTileCount = 4;  // 0 HBC, 1 Wii-Menü, 2 Einstellungen, 3 Ausschalten

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }
    void onEnter() override;
    void setSettings(Scene* settings) { settings_ = settings; }  // Ziel der Kachel „Einstellungen“

    int focus() const { return focus_; }
    gfx::Rect tileRect(const UiContext& ctx, int index) const;

private:
    int hitTest(const UiContext& ctx, float x, float y) const;

    Scene* settings_ = nullptr;
    int focus_ = 0;
    bool keysSincePointerMove_ = false;  // Richtungstaste nach der letzten Zeigerbewegung
};

}  // namespace nl::ui
