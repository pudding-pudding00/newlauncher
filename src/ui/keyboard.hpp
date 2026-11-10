#pragma once
// Bildschirmtastatur für die Suche (Spec 3.5): QWERTZ, Zeiger oder Steuerkreuz, Live-Filter.

#include <string>
#include <string_view>

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

    std::string_view text() const { return text_; }
    int keyCount() const;
    int keyIndex(char key) const;  // -1, wenn es die Taste nicht gibt
    int focusedKey() const { return focus_; }
    gfx::Rect keyRect(const UiContext& ctx, int index) const;

private:
    void press(int key, SceneRequests& out);
    int row(int key) const;

    SearchTarget& target_;
    std::string text_;
    int focus_ = 0;
    bool keysSincePointerMove_ = false;
};

}  // namespace nl::ui
