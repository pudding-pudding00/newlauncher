#pragma once
// Debug-Overlay (1+2 bzw. Z+Start): liegt über allem, nimmt keine Eingaben, zeigt Frame-Statistik.

#include "core/frame_stats.hpp"
#include "ui/scene.hpp"

namespace nl::ui {

class DebugOverlay {
public:
    void toggle() { visible_ = !visible_; }
    bool visible() const { return visible_; }
    void setStats(const FrameStats& s);  // Werte des vorigen Frames
    void draw(const UiContext& ctx, gfx::DrawList& dl) const;

private:
    bool visible_ = false;
    char text_[96] = "";
};

}  // namespace nl::ui
