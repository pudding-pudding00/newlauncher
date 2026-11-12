#pragma once
// Systeminfo hinter Einstellungen (M13, Spec v2 6.1): listet, was der Launcher über die Konsole weiß.
// A fordert SaveSysInfo an; main schreibt <data>/sysinfo.txt und meldet das Ergebnis über setSaveResult.

#include <string>
#include <string_view>
#include <vector>

#include "core/sysinfo.hpp"
#include "ui/scene.hpp"

namespace nl::ui {

class SysInfoScene final : public Scene {
public:
    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }
    void onEnter() override;  // Zeilen in der aktiven Sprache, oben, ohne Status

    void setInfo(SystemInfo info);
    const SystemInfo& info() const { return info_; }
    void setSaveResult(std::string_view path, bool ok);

    const std::vector<std::string>& lines() const { return lines_; }
    const std::string& status() const { return status_; }
    int scroll() const { return scroll_; }
    int maxScroll(const UiContext& ctx) const;

private:
    gfx::Rect cardRect(const UiContext& ctx) const;
    int visibleLines(const UiContext& ctx) const;

    SystemInfo info_;
    std::vector<std::string> lines_;
    std::string status_;
    int scroll_ = 0;
};

}  // namespace nl::ui
