#pragma once
// Einfacher Hinweisdialog (Spec 9, M3): Karte in der Mitte, Titel, Text mit Zeilenumbrüchen an '\n',
// ein Knopf „OK“. A, B oder ein Klick auf „OK“ schließt ihn. Kopiert die Texte in feste Puffer.

#include "ui/scene.hpp"

namespace nl::ui {

class DialogScene final : public Scene {
public:
    void open(const char* title, const char* text);
    const char* title() const { return title_; }
    const char* text() const { return text_; }

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }

    gfx::Rect buttonRect(const UiContext& ctx) const;

private:
    static constexpr int kMaxLines = 8;
    struct Layout;
    Layout layout(const UiContext& ctx) const;

    char title_[64] = "";
    char text_[256] = "";
};

}  // namespace nl::ui
