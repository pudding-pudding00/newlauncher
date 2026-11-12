#pragma once
// Einrichtungs-Assistent (M13, Spec v2 6.4): zeigt den Schritt aus SetupWizard. ▲▼ wählt, A übernimmt, B
// überspringt (Standardwert), − geht zurück, Home bricht ohne Schreiben ab. Im Schritt Netz fordert die Szene einmal
// SetupNetTest an; main meldet das Ergebnis über setNetStatus. In Fertig fordert A FinishSetup an.

#include <optional>

#include "core/setup_wizard.hpp"
#include "ui/scene.hpp"

namespace nl::ui {

class SetupScene final : public Scene {
public:
    enum class NetTest : u8 { Idle, Running, Ok, OkTimeSet, Fail };

    void start(SetupWizard wizard);  // neuer Durchlauf, Netzstatus zurückgesetzt
    const SetupWizard& wizard() const { return *wizard_; }
    SetupWizard& wizard() { return *wizard_; }  // nur nach start(); main treibt ihn im Smoke-Test (wizard = accept)
    bool active() const { return wizard_.has_value(); }

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }

    void setNetStatus(NetTest s) { net_ = s; }
    NetTest netStatus() const { return net_; }

private:
    gfx::Rect cardRect(const UiContext& ctx) const;

    std::optional<SetupWizard> wizard_;  // SetupWizard hat keinen Standardkonstruktor
    NetTest net_ = NetTest::Idle;
    bool netRequested_ = false;
};

}  // namespace nl::ui
