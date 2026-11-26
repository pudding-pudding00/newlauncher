#pragma once
// Kopplungsszene (M29, Dropbox): zeigt einen QR-Code zur Kopplungsseite auf der Wii (Handy öffnet sie, lässt bei
// Dropbox zu und schickt den Code zurück) oder – nach Y/1 bzw. ohne Kopplungsserver – direkt zur Dropbox-Freigabe.
// A meldet CloudEnterCode (main öffnet die Tastatur), B schließt mit CloudCancelLink. main schließt die Szene selbst
// per requestClose(), sobald die Anmeldung fertig ist.

#include <optional>
#include <string>

#include "core/qr.hpp"
#include "ui/scene.hpp"

namespace nl::ui {

class PairingScene final : public Scene {
public:
    void open();
    // pairUrl leer = Kopplungsserver läuft nicht → nur der direkte QR.
    void setLink(std::string pairUrl, std::string authorizeUrl, u32 expiresInS);
    void setStatus(std::string s) { status_ = std::move(s); }
    void requestClose() { closeRequested_ = true; }
    bool waiting() const { return authorizeUrl_.empty(); }
    bool directQr() const { return direct_ || pairUrl_.empty(); }
    const std::string& qrText() const { return directQr() ? authorizeUrl_ : pairUrl_; }
    std::string remainingText() const;  // "mm:ss"

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }

private:
    std::string pairUrl_, authorizeUrl_, status_;
    std::optional<QrCode> pairQr_, directQr_;
    float remainingMs_ = 0.0f;
    bool direct_ = false;
    bool closeRequested_ = false;
};

}  // namespace nl::ui
