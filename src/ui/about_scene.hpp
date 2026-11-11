#pragma once
// Seite „Über“ hinter Einstellungen (M11, Spec v2 3.4): Version, Build-Option HTTPS, GPLv3-Hinweis und die
// Drittanbieter-Komponenten mit Lizenz. Die Liste entsteht zur Build-Zeit aus THIRD_PARTY.md
// (cmake/NlAboutEntries.cmake); die Volltexte stehen in apps/newlauncher/THIRD_PARTY.txt.

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ui/scene.hpp"

namespace nl::ui {

struct AboutEntry {
    const char* name;
    const char* version;
    const char* license;
};

std::span<const AboutEntry> aboutEntries();

class AboutScene final : public Scene {
public:
    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }
    void onEnter() override;  // setzt die Zeilen in der aktiven Sprache neu und blättert nach oben

    void setBuildInfo(std::string_view version, bool https);

    const std::vector<std::string>& lines() const { return lines_; }
    int scroll() const { return scroll_; }
    int maxScroll(const UiContext& ctx) const;

private:
    gfx::Rect cardRect(const UiContext& ctx) const;
    int visibleLines(const UiContext& ctx) const;
    void rebuild();

    std::string version_;
    bool https_ = false;
    std::vector<std::string> lines_;
    int headingLine_ = 0;  // Index der Zeile „Enthaltene Komponenten“
    int scroll_ = 0;
};

}  // namespace nl::ui
