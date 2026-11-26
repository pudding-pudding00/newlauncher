#pragma once
// Cloud-Menü (M27) aus den Einstellungen: Zeile Anbieter (Aus/Google Drive, ◀▶ oder A), Anmelden, Abmelden und eine
// Statuszeile. Gesperrte Zeilen überspringt der Fokus. Die Arbeit erledigt main (CloudWorker) und meldet den Stand
// über setView/setStatus zurück.

#include <string>

#include "core/config.hpp"
#include "ui/scene.hpp"

namespace nl::ui {

struct CloudView {
    enum class Status : u8 { NotLinked, Linked, Relink, NoTls, NoClientId };
    CloudProviderId provider = CloudProviderId::None;
    Status status = Status::NotLinked;
    bool busy = false;  // Anmelden oder Abgleich läuft
};

class CloudScene final : public Scene {
public:
    static constexpr int kRowProvider = 0, kRowLink = 1, kRowUnlink = 2, kRowCount = 3;

    void open(CloudView v);  // Fokus auf die erste freie Zeile, Meldung leer
    void setView(CloudView v);
    const CloudView& view() const { return view_; }
    void setStatus(std::string s) { message_ = std::move(s); }
    const std::string& message() const { return message_; }

    CloudProviderId provider() const { return view_.provider; }  // nach CloudSetProvider der neue Wert
    int focus() const { return focus_; }
    bool rowEnabled(int row) const;
    const char* statusText() const;

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }

    gfx::Rect rowRect(const UiContext& ctx, int row) const;

private:
    void fixFocus();
    gfx::Rect cardRect(const UiContext& ctx) const;

    CloudView view_;
    std::string message_;
    int focus_ = 0;
};

// Konfliktzeile "Wii: <Datum Zeit> · Cloud: <Datum Zeit>, <Gerät>" (UTC, DE dd.mm.yyyy, EN yyyy-mm-dd); 0 = unbekannt.
std::string conflictText(u64 wiiPlayed, u64 cloudSaved, std::string_view device);

}  // namespace nl::ui
