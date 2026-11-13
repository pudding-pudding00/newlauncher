#pragma once
// A–Z-Sprungleiste (Spec 3.5): Overlay mit "#", "A"–"Z" und "Suche" in einer Reihe über der Infoleiste.
// Darüber die Filterzeile (M17, Spec 10.3): Hoch/Runter wechselt die Reihe, Links/Rechts/A blättern den Filter.

#include <string>

#include "ui/scene.hpp"

namespace nl::ui {

class JumpTarget {
public:
    virtual ~JumpTarget() = default;
    virtual void jumpToBucket(char bucket) = 0;  // '#' oder 'A'–'Z'
    virtual Scene* openSearch() = 0;  // Szene, die nach dem Schließen der Leiste geöffnet wird (Tastatur)
    virtual bool hasBucket(char bucket) const = 0;
    virtual char currentBucket() const = 0;  // Buchstabe des fokussierten Spiels, sonst '#'
    // Filterzeile: 0 Filter = keine Zeile. setFilterIndex wirkt sofort auf die Liste.
    virtual int filterCount() const { return 0; }
    virtual int filterIndex() const { return 0; }
    virtual std::string filterLabel(int) const { return {}; }
    virtual void setFilterIndex(int) {}
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
    bool filterRowFocused() const { return filterRow_; }
    gfx::Rect cellRect(const UiContext& ctx, int index) const;
    gfx::Rect filterRect(const UiContext& ctx) const;  // liegt über der Buchstabenreihe

private:
    void stepFilter(int dir, SceneRequests& out);

    JumpTarget& target_;
    int focus_ = 0;
    bool filterRow_ = false;
    bool keysSincePointerMove_ = false;
};

}  // namespace nl::ui
