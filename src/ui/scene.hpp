#pragma once
// Szenen (Spec 4.1): ein Stapel aus Vollbild-Szenen und Overlays; Overlays blenden 200 ms ein/aus.

#include "core/easing.hpp"
#include "core/theme.hpp"
#include "gfx/draw_list.hpp"
#include "gfx/font.hpp"
#include "gfx/shapes.hpp"
#include "ui/input.hpp"

namespace nl::ui {

// Pro Frame von main befüllt; Szenen lesen nur.
struct UiContext {
    float width = 640.0f, height = 480.0f;
    const Theme* theme = nullptr;
    const gfx::FontSet* fonts = nullptr;
    const gfx::AtlasShapes* shapes = nullptr;
    ThemeMode themeMode = ThemeMode::Auto;
    int minuteOfDay = 0;  // 0..1439
    int day = 1, month = 1, year = 2026, weekday = 0;  // weekday 0 = Montag
};

enum class AppCommand : u8 { None, ExitToHbc, WiiMenu, PowerOff, CycleTheme };
const char* appCommandName(AppCommand c);

class Scene;
struct SceneRequests {
    Scene* push = nullptr;
    bool pop = false;
    AppCommand command = AppCommand::None;
};

class Scene {
public:
    virtual ~Scene() = default;
    virtual void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) = 0;
    virtual void draw(const UiContext& ctx, gfx::DrawList& dl) = 0;
    virtual bool isOverlay() const { return false; }
    virtual void onEnter() {}
};

inline constexpr float kOverlayFadeMs = 200.0f;

// Besitzt keine Szenen; feste Tiefe 4.
class SceneStack {
public:
    void push(Scene* s);
    AppCommand update(const UiContext& ctx, const InputFrame& in, float dtMs);
    void draw(const UiContext& ctx, gfx::DrawList& dl);
    Scene* top() const { return depth_ > 0 ? entries_[depth_ - 1].scene : nullptr; }
    int depth() const { return depth_; }

private:
    static constexpr int kMaxDepth = 4;
    struct Entry {
        Scene* scene = nullptr;
        Tween alpha;
        bool popping = false;
    };
    int lowestVisible() const;  // unterste Szene, die gezeichnet/aktualisiert wird

    Entry entries_[kMaxDepth];
    int depth_ = 0;
};

}  // namespace nl::ui
