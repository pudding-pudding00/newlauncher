#pragma once
// Kindersicherung hinter Einstellungen → „Kindersicherung“ (M17, Spec v2 10.5). Ändert die Live-Config direkt
// (SettingsChanged), B schließt und meldet SaveSettings bei Änderungen. „PIN festlegen/ändern“ meldet ChangePin,
// „PIN entfernen“ RemovePin; main führt beides aus (die Szene bleibt offen). Mit PIN öffnet main sie erst nach der
// PIN-Abfrage.

#include <string>

#include "core/parental.hpp"
#include "ui/scene.hpp"

namespace nl::ui {

class ParentalScene final : public Scene {
public:
    static constexpr int kCount = static_cast<int>(ParentalRow::Count);

    void setConfig(ParentalConfig* live) { live_ = live; }
    int focus() const { return focus_; }
    std::string valueTextFor(ParentalRow row) const;

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }
    void onEnter() override;

private:
    gfx::Rect cardRect(const UiContext& ctx) const;
    gfx::Rect rowRect(const UiContext& ctx, int row) const;

    ParentalConfig* live_ = nullptr;
    ParentalConfig saved_;
    int focus_ = 0;
};

}  // namespace nl::ui
