#pragma once
// Bildschirmtastatur für die Suche (Spec 3.5): QWERTZ, Zeiger oder Steuerkreuz, Live-Filter. Ab M29 auch für Codes:
// KeyboardMode schaltet Ebenen (abc/ABC/123/#), eine längere Grenze und Maskierung zu.
// Darüber die Filterzeile (Spec v2 10.3) wie in der A–Z-Leiste: Hoch aus der obersten Tastenreihe wählt sie,
// Links/Rechts/A blättern, Runter geht zurück auf die Tasten.

#include <string>
#include <string_view>
#include <utility>

#include "ui/filter_source.hpp"
#include "ui/scene.hpp"

namespace nl::ui {

class TextTarget {
public:
    virtual ~TextTarget() = default;
    virtual void setText(std::string_view text) = 0;       // nach jeder Änderung
    virtual void submitText(std::string_view /*text*/) {}  // „Fertig“ (nicht bei B)
};

struct KeyboardMode {
    std::size_t maxLen = 32;
    bool layers = false;  // Ebenen-Taste abc/ABC/123/#; ohne: QWERTZ groß wie in der Suche
    bool mask = false;    // Feld zeigt • je Zeichen
    std::string hint;     // leer → „Suche“-Hinweis
};

class Keyboard final : public Scene {
public:
    static constexpr char kSpace = '\x01', kBackspace = '\x02', kDone = '\x03', kLayer = '\x04';
    static constexpr std::size_t kMaxText = 32;

    explicit Keyboard(TextTarget& target) : target_(&target) {}
    void setTarget(TextTarget& target) { target_ = &target; }
    void setMode(KeyboardMode mode) { mode_ = std::move(mode); }
    void requestClose() { closeRequested_ = true; }
    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }
    void onEnter() override;  // neue Suche: leerer Text, Fokus auf der ersten Taste
    void setFilter(FilterSource* filter) { filter_ = filter; }  // nullptr oder 0 Filter = keine Filterzeile

    std::string_view text() const { return text_; }
    std::string shownText() const;  // was das Feld zeigt: Text, • je Zeichen oder der Hinweis
    std::string visibleText(const gfx::FontFace& face, float maxWidth) const;
    std::string_view layerName() const;
    int keyCount() const;
    int keyIndex(char key) const;  // -1, wenn es die Taste nicht gibt
    int focusedKey() const { return focus_; }
    gfx::Rect keyRect(const UiContext& ctx, int index) const;
    bool filterRowFocused() const { return filterRow_; }
    gfx::Rect filterRect(const UiContext& ctx) const;  // über dem Tastaturfeld

private:
    void press(int key, SceneRequests& out);
    int row(int key) const;
    int rowCount() const;
    int rowLength(int r) const;
    int rowStart(int r) const;
    char keyChar(int index) const;
    float rowWidth(int r) const;
    const char* const* rows() const;
    bool hasFilter() const { return filter_ != nullptr && filter_->filterCount() > 0; }
    void stepFilter(int dir, SceneRequests& out);

    TextTarget* target_;
    KeyboardMode mode_;
    std::string text_;
    int layer_ = 0;
    FilterSource* filter_ = nullptr;
    int focus_ = 0;
    bool filterRow_ = false;
    bool keysSincePointerMove_ = false;
    bool closeRequested_ = false;
};

}  // namespace nl::ui
