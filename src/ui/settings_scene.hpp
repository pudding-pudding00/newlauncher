#pragma once
// Einstellungen hinter HOME → „Einstellungen“ (Spec 3.4): eine Liste aus Werte- und Aktionszeilen.
// Die Szene ändert die Live-Config direkt; main wendet sie bei SettingsChanged an und schreibt sie bei
// SaveSettings. Overlay-Karte; Steuerkreuz oder Zeiger wählt, ◀▶/A ändert, B schließt.

#include "core/config.hpp"
#include "core/settings_model.hpp"
#include "ui/scene.hpp"

namespace nl::ui {

class SettingsScene final : public Scene {
public:
    static constexpr int kCount = static_cast<int>(SettingRow::Count);

    // Beim Start aufrufen: die Quellen der Live-Config gelten als die laufenden (Hinweis „nach Neustart“).
    void setConfig(Config* live) {
        live_ = live;
        if (live != nullptr) bootSources_ = live->libraryRules.sources;
    }

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }
    void onEnter() override;

    int focus() const { return focus_; }
    void setThemePacks(std::vector<std::string> packs) { themePacks_ = std::move(packs); }  // gefundene Theme-Pakete
    void setActiveSources(const SourceSpec& running) { bootSources_ = running; }  // laufende Quellen (Rückfall auf SD)
    bool restartHintShown() const { return live_ != nullptr && !(live_->libraryRules.sources == bootSources_); }
    std::string valueTextFor(SettingRow row) const;  // angezeigter Wert einer Zeile (leer bei Aktionen)
    gfx::Rect rowRect(const UiContext& ctx, int row) const;

private:
    gfx::Rect cardRect(const UiContext& ctx) const;
    int hitTest(const UiContext& ctx, float x, float y) const;
    bool dirty() const;  // *live_ weicht vom zuletzt gespeicherten Stand ab

    Config* live_ = nullptr;
    std::vector<std::string> themePacks_;
    Config saved_;       // Stand bei onEnter bzw. nach dem letzten SaveSettings
    SourceSpec bootSources_;  // beim Start gültige Bibliotheksquellen
    AppCommand pendingAction_ = AppCommand::None;  // Aktion nach SaveSettings im nächsten Frame
    Console pendingConsole_ = Console::Wii;        // OpenOptions: Wii- oder GameCube-Spiele
    int focus_ = 0;
    bool armed_ = false;  // M23: „Cover-Cache löschen“ wartet auf das zweite A
    bool keysSincePointerMove_ = false;
};

}  // namespace nl::ui
