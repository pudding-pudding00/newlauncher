#pragma once
// Gerätecode-Szene (M27): zeigt die Adresse und den Code zum Anmelden auf Handy oder PC, groß und mit Restzeit.
// open() zeigt zuerst „Code wird angefordert“, setCode() dann den Code. B schließt und meldet CloudCancelLink.
// main schließt sie selbst, sobald CloudWorker::linkState() fertig ist.

#include <string>

#include "ui/scene.hpp"

namespace nl::ui {

class DeviceCodeScene final : public Scene {
public:
    void open();
    void setCode(std::string verifyUrl, std::string userCode, u32 expiresInS);
    void setStatus(std::string s) { status_ = std::move(s); }
    bool waiting() const { return userCode_.empty(); }
    void requestClose() { closeRequested_ = true; }  // nächster update schließt ohne CloudCancelLink
    std::string remainingText() const;  // "mm:ss", aufgerundet, nie negativ

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }

private:
    std::string url_, userCode_, status_;
    float remainingMs_ = 0.0f;
    bool closeRequested_ = false;
};

}  // namespace nl::ui
