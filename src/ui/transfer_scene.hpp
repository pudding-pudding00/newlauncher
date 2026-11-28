#pragma once
// Foreground File Transfer (FTP) overlay scene (M32).

#include <string>
#include <vector>

#include "core/ftp_vfs.hpp"
#include "ui/scene.hpp"

namespace nl::ui {

struct TransferView {
    bool listening = false;
    bool clientConnected = false;
    bool transferring = false;
    bool customPassword = false;
    std::string uri;
    std::string user;
    std::string pin;
    std::string peerIp;
    std::string activeOp;
    std::string activePath;
    u64 activeBytes = 0;
    std::vector<FtpMount> mounts;
};

class TransferScene final : public Scene {
public:
    void open(TransferView view);
    void setView(TransferView view) { view_ = std::move(view); }
    const TransferView& view() const { return view_; }

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }

private:
    TransferView view_;
};

}  // namespace nl::ui
