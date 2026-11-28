#pragma once
// Confirmation overlay for incoming wiiload v0.5 payloads (M31).

#include <string>
#include <vector>

#include "core/wiiload_proto.hpp"
#include "ui/scene.hpp"

namespace nl::ui {

struct WiiloadSummary {
    std::string senderIp;
    WiiloadKind kind = WiiloadKind::Dol;
    std::string fileName;
    u32 sizeBytes = 0;
    std::vector<std::string> args;
    std::string targetApp;  // non-empty for apps/<name>/ ZIP bundles
};

class WiiloadScene final : public Scene {
public:
    static constexpr int kButtonCount = 3;  // 0 = Run, 1 = Trust sender, 2 = Decline

    void open(WiiloadSummary summary);
    const WiiloadSummary& summary() const { return summary_; }
    int focused() const { return focus_; }

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }

private:
    WiiloadSummary summary_;
    int focus_ = 0;
};

}  // namespace nl::ui
