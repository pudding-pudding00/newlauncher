#pragma once
// Aktionsliste hinter HOME → „Einstellungen“ (Spec 3.3, 6.4): bis zum Einstellungsdialog in M6 nur
// „VC-Injects zuordnen“ und „Fehlende Cover laden“. Overlay-Karte; Steuerkreuz oder Zeiger, A bestätigt, B schließt.

#include "ui/scene.hpp"

namespace nl::ui {

class ActionsScene final : public Scene {
public:
    static constexpr int kCount = 2;  // 0 VC-Injects zuordnen, 1 Fehlende Cover laden

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }
    void onEnter() override;

    int focus() const { return focus_; }
    gfx::Rect rowRect(const UiContext& ctx, int index) const;

private:
    gfx::Rect cardRect(const UiContext& ctx) const;
    int hitTest(const UiContext& ctx, float x, float y) const;

    int focus_ = 0;
    bool keysSincePointerMove_ = false;
};

}  // namespace nl::ui
