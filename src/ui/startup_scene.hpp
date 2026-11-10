#pragma once
// Startszene (Spec 6.1): Splash, „Warte auf USB …", Dialog „Erneut versuchen / SD verwenden",
// Fortschritt des ersten Scans. main steuert die Zustände, die Szene meldet nur die Wahl zurück.

#include "ui/scene.hpp"

namespace nl::ui {

class StartupScene final : public Scene {
public:
    enum class Choice : u8 { None, Retry, UseSd };

    void showWaitingUsb(int secondsLeft);
    void showUsbFailed();
    void showScanning(u32 filesSeen);
    Choice takeChoice();  // einmalig, danach None

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;

    gfx::Rect buttonRect(const UiContext& ctx, int index) const;  // 0 Erneut versuchen, 1 SD verwenden

private:
    enum class Phase : u8 { Splash, WaitingUsb, UsbFailed, Scanning };

    Phase phase_ = Phase::Splash;
    char status_[64] = "";
    int focus_ = 0;
    bool keysSincePointerMove_ = false;
    Choice choice_ = Choice::None;
};

}  // namespace nl::ui
