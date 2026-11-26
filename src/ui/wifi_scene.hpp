#pragma once
// WLAN-Info (M25, Spec 2026-11-26-m25-wlan-design.md 3): Overlay hinter dem WLAN-Symbol der Infoleiste.
// main reicht jeden Frame den Stand des Netz-Threads herein; A fordert WifiReconnect an, B/Home schließt.

#include <string>
#include <vector>

#include "core/wifi_state.hpp"
#include "ui/scene.hpp"

namespace nl::ui {

// Zeilen des Fensters in der aktiven Sprache; eingerückte Zeilen (führendes Leerzeichen) sind Tipps.
std::vector<std::string> wifiInfoLines(const WifiSnapshot& s, u64 nowMs);
const char* wifiStatusText(WifiStatus s);

class WifiScene final : public Scene {
public:
    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }
    void onEnter() override { scroll_ = 0; }

    void setSnapshot(const WifiSnapshot& s, u64 nowMs);
    const WifiSnapshot& snapshot() const { return snap_; }
    const std::vector<std::string>& lines() const { return lines_; }
    int scroll() const { return scroll_; }
    int maxScroll(const UiContext& ctx) const;

private:
    gfx::Rect cardRect(const UiContext& ctx) const;
    int visibleLines(const UiContext& ctx) const;

    WifiSnapshot snap_;
    std::vector<std::string> lines_;
    int scroll_ = 0;
};

}  // namespace nl::ui
