#pragma once
// Spielstand-Szene (M26) über den Spieldetails: zeigt Einheit, Ort, Größe und die lokalen Sicherungen eines Spiels.
// Hoch/Runter wählt eine Sicherung, Links/Rechts einen Knopf, A meldet BackupSave/RestoreSave/DeleteBackup, B schließt.
// Die Arbeit erledigt main (SaveWorker) und meldet den neuen Stand über setView/setStatus. Ist der NAND gesperrt oder
// der Ort unbekannt, zeigt sie nur Text; solange ein Auftrag läuft (busy), sind alle Knöpfe gesperrt.

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ui/scene.hpp"

namespace nl::ui {

struct SavesView {
    enum class State : u8 { Ok, Empty, NandLocked, Unknown };
    std::string unitId, location;  // z. B. "wii-RMGP", "nand:/title/00010000/524d4750/data"
    u64 bytes = 0;
    u32 files = 0;
    State state = State::Unknown;
    std::vector<std::string> backups;  // Anzeigetexte, neueste zuerst; Index = selectedBackup()
    bool busy = false;
};

class SavesScene final : public Scene {
public:
    static constexpr int kButtonBackup = 0, kButtonRestore = 1, kButtonDelete = 2, kButtonCount = 3;
    static constexpr int kVisibleRows = 5;

    void open(const Game& g, SavesView v);  // leert die Meldung, wählt die neueste Sicherung
    void setView(SavesView v);              // behält Auswahl und Knopf, soweit noch gültig
    void setStatus(std::string s) { status_ = std::move(s); }
    const std::string& status() const { return status_; }
    const SavesView& view() const { return view_; }
    const Game* game() const { return hasGame_ ? &game_ : nullptr; }

    int selectedBackup() const { return selected_; }  // -1 = keine Sicherung
    int focusedButton() const { return focus_; }      // -1 = keine Knöpfe
    bool buttonEnabled(int b) const { return !view_.busy && buttonUsable(b); }
    const char* stateText() const;  // Zeile unter dem Ort: Größe bzw. Grund

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }

    gfx::Rect buttonRect(const UiContext& ctx, int b) const;

private:
    bool buttonUsable(int b) const;  // ohne Rücksicht auf busy
    bool hasButtons() const;
    void clampSelection();
    gfx::Rect cardRect(const UiContext& ctx) const;

    std::string key_, title_;
    Game game_{};
    bool hasGame_ = false;
    SavesView view_;
    std::string status_;
    mutable char stateText_[96] = "";
    int selected_ = -1, scroll_ = 0, focus_ = -1;
};

// "12 KB", "1,5 MB" (DE) bzw. "1.5 MB" (EN); unter 1 KiB "N B".
std::string formatBytes(u64 bytes);
// "20261126-101507.zip" → DE "26.11.2026 10:15:07", EN "2026-11-26 10:15:07"; anderes bleibt, wie es ist.
std::string backupLabel(std::string_view fileName);

}  // namespace nl::ui
