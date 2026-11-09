#pragma once
// A–Z-Sprungleiste (Spec 3.5): Overlay mit "#", "A"–"Z" und "Suche" in einer Reihe über der Infoleiste.

#include "ui/scene.hpp"

namespace nl::ui {

class JumpTarget {
public:
    virtual ~JumpTarget() = default;
    virtual void jumpToBucket(char bucket) = 0;  // '#' oder 'A'–'Z'
    virtual void openSearch() = 0;
    virtual bool hasBucket(char bucket) const = 0;
    virtual char currentBucket() const = 0;  // Buchstabe des fokussierten Spiels, sonst '#'
};

class JumpBar final : public Scene {
public:
    static constexpr int kCellCount = 28;  // 0 = "#", 1–26 = A–Z, 27 = Suche
    static constexpr int kSearchCell = 27;

    explicit JumpBar(JumpTarget& target) : target_(target) {}
    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }
    void onEnter() override;

    int focus() const { return focus_; }
    gfx::Rect cellRect(const UiContext& ctx, int index) const;

private:
    JumpTarget& target_;
    int focus_ = 0;
    bool keysSincePointerMove_ = false;
};

}  // namespace nl::ui
