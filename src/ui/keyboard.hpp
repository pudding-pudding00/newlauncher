#pragma once
// Bildschirmtastatur für die Suche (Spec 3.5): QWERTZ, Zeiger oder Steuerkreuz, Live-Filter.
// Darüber die Filterzeile (Spec v2 10.3) wie in der A–Z-Leiste: Hoch aus der obersten Tastenreihe wählt sie,
// Links/Rechts/A blättern, Runter geht zurück auf die Tasten.

#include <string>
#include <string_view>

#include "ui/filter_source.hpp"
#include "ui/scene.hpp"

namespace nl::ui {

class SearchTarget {
public:
    virtual ~SearchTarget() = default;
    virtual void setQuery(std::string_view query) = 0;
};

class Keyboard final : public Scene {
public:
    static constexpr char kSpace = '\x01', kBackspace = '\x02', kDone = '\x03';
    static constexpr std::size_t kMaxText = 32;

    explicit Keyboard(SearchTarget& target) : target_(target) {}
    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }
    void onEnter() override;  // neue Suche: leerer Text, Fokus auf der ersten Taste
    void setFilter(FilterSource* filter) { filter_ = filter; }  // nullptr oder 0 Filter = keine Filterzeile

    std::string_view text() const { return text_; }
    int keyCount() const;
    int keyIndex(char key) const;  // -1, wenn es die Taste nicht gibt
    int focusedKey() const { return focus_; }
    gfx::Rect keyRect(const UiContext& ctx, int index) const;
    bool filterRowFocused() const { return filterRow_; }
    gfx::Rect filterRect(const UiContext& ctx) const;  // über dem Tastaturfeld

private:
    void press(int key, SceneRequests& out);
    int row(int key) const;
    bool hasFilter() const { return filter_ != nullptr && filter_->filterCount() > 0; }
    void stepFilter(int dir, SceneRequests& out);

    SearchTarget& target_;
    std::string text_;
    FilterSource* filter_ = nullptr;
    int focus_ = 0;
    bool filterRow_ = false;
    bool keysSincePointerMove_ = false;
};

}  // namespace nl::ui
