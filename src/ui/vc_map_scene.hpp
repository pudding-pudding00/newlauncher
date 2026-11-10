#pragma once
// Dialog „VC-Injects zuordnen“ (Spec 6.6): je N64-Spiel ein installierter Kanal oder keiner.
// Vollbildszene; die App liest beim Schließen (SaveVcMap) die Auswahl und schreibt n64_vc.ini.

#include <optional>
#include <string>
#include <vector>

#include "core/vc_map.hpp"
#include "ui/scene.hpp"

namespace nl::ui {

struct VcMapRow {
    std::string key;    // Bibliotheksschlüssel des Spiels
    std::string title;  // Anzeigetitel
    std::optional<u64> mapped;
    std::optional<std::size_t> suggestion;  // Index in channels
};

class VcMapScene final : public Scene {
public:
    static constexpr int kVisibleRows = 7;

    void open(std::vector<VcMapRow> rows, std::vector<ChannelInfo> channels);
    int focusedRow() const { return focus_; }
    std::optional<u64> selection(int row) const;
    bool dirty() const { return dirty_; }
    const std::vector<VcMapRow>& rows() const { return rows_; }  // mapped = aktuelle Auswahl
    std::size_t channelCount() const { return channels_.size(); }

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;

private:
    int channelIndex(std::optional<u64> id) const;  // -1 für keine oder unbekannte ID
    void select(int row, std::optional<u64> id);
    void refreshText(int row);

    std::vector<VcMapRow> rows_;
    std::vector<ChannelInfo> channels_;
    std::vector<std::string> right_;  // Text der rechten Spalte je Zeile
    int focus_ = 0;
    int scroll_ = 0;
    bool dirty_ = false;
};

}  // namespace nl::ui
