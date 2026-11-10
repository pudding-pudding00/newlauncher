#pragma once
// Einstellungen hinter HOME → „Einstellungen“ (Spec 3.4): eine Liste aus Werte- und Aktionszeilen.
// Die Szene ändert die Live-Config direkt; main wendet sie bei SettingsChanged an und schreibt sie bei
// SaveSettings. Overlay-Karte; Steuerkreuz oder Zeiger wählt, ◀▶/A ändert, B schließt.

#include "core/config.hpp"
#include "core/settings_model.hpp"
#include "ui/scene.hpp"

namespace nl::ui {

class SettingsScene final : public Scene {
public:
    static constexpr int kCount = static_cast<int>(SettingRow::Count);

    void setConfig(Config* live) { live_ = live; }

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }
    void onEnter() override;

    int focus() const { return focus_; }
    gfx::Rect rowRect(const UiContext& ctx, int row) const;

private:
    gfx::Rect cardRect(const UiContext& ctx) const;
    int hitTest(const UiContext& ctx, float x, float y) const;
    bool dirty() const;  // *live_ weicht vom zuletzt gespeicherten Stand ab

    Config* live_ = nullptr;
    Config saved_;       // Stand bei onEnter bzw. nach dem letzten SaveSettings
    Config entered_;     // Stand bei onEnter (Hinweis „nach Neustart“ beim Bibliotheksgerät)
    AppCommand pendingAction_ = AppCommand::None;  // Aktion nach SaveSettings im nächsten Frame
    int focus_ = 0;
    bool keysSincePointerMove_ = false;
};

}  // namespace nl::ui
