#pragma once
// Sichtbarer Zeiger-Cursor (Spec 3.8): wird als Letztes gezeichnet, über allen Szenen und Overlays.

#include "ui/scene.hpp"

namespace nl::ui {

void drawPointerCursor(const UiContext& ctx, gfx::DrawList& dl, const InputFrame& in);

}  // namespace nl::ui
