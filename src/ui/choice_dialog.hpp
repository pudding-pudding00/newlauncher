#pragma once
// Auswahldialog (M26): wie DialogScene, aber mit zwei oder drei Knöpfen nebeneinander. Links/Rechts wechselt den
// Fokus, A wählt, B bricht ab (picked() = -1). In beiden Fällen schließt er sich und sendet onPick; main liest
// danach picked(). Texte liegen in festen Puffern.

#include <span>

#include "ui/scene.hpp"

namespace nl::ui {

class ChoiceDialog final : public Scene {
public:
    static constexpr int kMaxButtons = 3;

    void open(const char* title, const char* text, std::span<const char* const> buttons, AppCommand onPick,
              int defaultIndex = 0);
    int picked() const { return picked_; }
    int focused() const { return focus_; }
    int buttonCount() const { return count_; }

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }

    gfx::Rect buttonRect(const UiContext& ctx, int index) const;

private:
    static constexpr int kMaxLines = 8;
    struct Layout;
    Layout layout(const UiContext& ctx) const;

    char title_[64] = "";
    char text_[256] = "";
    char labels_[kMaxButtons][40] = {};
    int count_ = 0;
    int focus_ = 0;
    int picked_ = -1;
    AppCommand onPick_ = AppCommand::None;
};

}  // namespace nl::ui
