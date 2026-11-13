#pragma once
// Spieloptionen (Spec 7): pro Spiel aus den Details (+ bzw. GC-R) oder global aus den Einstellungen
// („Wii-Spiele“, „GameCube-Spiele“). Die Szene zeichnet und übersetzt Werte; Logik und Dateitexte
// liegen im OptionsModel. B schließt und meldet SaveOptions nur, wenn das Modell geändert wurde;
// main schreibt dann games.ini bzw. config.ini atomar.

#include <optional>
#include <string>

#include "core/options_model.hpp"
#include "ui/scene.hpp"

namespace nl::ui {

class OptionsScene final : public Scene {
public:
    // title: Spieltitel (pro Spiel); leer = Konsolenname bzw. „Wii-Spiele“/„GameCube-Spiele“.
    void open(OptionsModel model, std::string title);

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }
    void onEnter() override;

    void requestClose() { closeRequested_ = true; }  // nächster update schließt wie B (Smoke-Tests)
    bool hasModel() const { return model_.has_value(); }
    OptionsModel& model() { return *model_; }
    int rowCount() const { return model_ ? static_cast<int>(model_->rows()) + 1 : 0; }  // + Zurücksetzen
    int focus() const { return focus_; }
    std::string titleText() const;
    std::string labelText(int row) const;
    std::string valueText(int row) const;
    gfx::Rect rowRect(const UiContext& ctx, int row) const;

private:
    gfx::Rect cardRect(const UiContext& ctx) const;
    int hitTest(const UiContext& ctx, float x, float y) const;
    bool isResetRow(int row) const { return model_ && row == static_cast<int>(model_->rows()); }

    std::optional<OptionsModel> model_;
    std::string title_;
    int focus_ = 0;
    bool closeRequested_ = false;
    bool keysSincePointerMove_ = false;
};

}  // namespace nl::ui
