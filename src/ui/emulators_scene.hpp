#pragma once
// Emulator-Verwaltung hinter Einstellungen → „Emulatoren“ (Spec 6.9): je Emulator eine Zeile mit Status,
// darunter „Nach Updates suchen“ und „Alle aktualisieren“. Die Szene löst nur Befehle aus; main lädt die
// Liste und installiert über den EmuWorker und meldet Zeilen, Fortschritt und Fehler zurück.
// Kein Netz beim Öffnen.

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/emu_catalog.hpp"
#include "core/emu_install.hpp"
#include "core/net_error.hpp"
#include "ui/scene.hpp"

namespace nl::ui {

struct EmuProgress {
    std::string name;
    EmuStep step = EmuStep::Download;
    u32 done = 0, total = 0;
    u32 index = 0, count = 1;  // laufende App von count
};

class EmulatorsScene final : public Scene {
public:
    enum class ListState : u8 { NotLoaded, Loading, Loaded, Failed };
    static constexpr int kEmuRows = 6;
    static constexpr int kRowCount = kEmuRows + 2;  // + Nach Updates suchen, Alle aktualisieren

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }
    void onEnter() override;

    void setRows(std::vector<EmuRow> rows) { rows_ = std::move(rows); }
    void setBusy(std::optional<EmuProgress> p) { busy_ = std::move(p); }
    // Failed mit Ursache (Spec v2 4.4): Other zeigt nur den allgemeinen Text.
    void setListState(ListState s, NetError cause = NetError::Other) {
        list_ = s;
        listCause_ = cause;
    }
    ListState listState() const { return list_; }
    void setError(std::string_view oscName, EmuInstallError e);

    int focus() const { return focus_; }
    const EmuRow* selectedRow() const;
    std::vector<OscApp> takeTargets();  // Ziele des letzten EmuInstall/EmuInstallAll
    std::string statusText(int row) const;
    std::string infoText() const;  // Infozeile ohne laufenden Auftrag
    gfx::Rect rowRect(const UiContext& ctx, int row) const;

private:
    bool working() const { return busy_.has_value() || list_ == ListState::Loading; }
    gfx::Rect cardRect(const UiContext& ctx) const;
    int hitTest(const UiContext& ctx, float x, float y) const;

    std::vector<EmuRow> rows_;
    std::optional<EmuProgress> busy_;
    ListState list_ = ListState::NotLoaded;
    NetError listCause_ = NetError::Other;
    std::map<std::string, EmuInstallError, std::less<>> errors_;
    std::vector<OscApp> targets_;
    int focus_ = 0;
    bool keysSincePointerMove_ = false;
};

}  // namespace nl::ui
