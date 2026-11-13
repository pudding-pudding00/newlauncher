#pragma once
// Cheat-Auswahl (M15, Spec v2 8): Vollbildliste der Gecko-Codes eines Spiels. A schaltet einen Code um, ◀▶ blättert,
// +/R lädt die Codeliste (eine vorhandene Datei erst nach Nachfrage), B schließt und meldet SaveCheats nur bei
// Änderung. Laden und Schreiben erledigt main (CheatWorker bzw. .sel atomar); die Szene hält nur Modell und Zustand.

#include <optional>
#include <string>

#include "core/cheat_model.hpp"
#include "core/cheat_source.hpp"
#include "ui/scene.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

class CheatsScene final : public Scene {
public:
    static constexpr int kVisibleRows = 8;

    // fileExists: <ID6>.txt liegt vor (auch unlesbar) → Laden fragt nach. fetchBlocked: Grund, warum Laden nicht geht.
    void open(CheatModel model, std::string id6, std::string title, bool fileExists, std::optional<Str> fetchBlocked);
    // Ergebnis des CheatWorkers; reloaded = neu eingelesenes Modell bei Ok/Exists.
    void fetchDone(CheatFetchResult result, std::optional<CheatModel> reloaded);

    bool hasModel() const { return model_.has_value(); }
    CheatModel& model() { return *model_; }
    const std::string& id6() const { return id6_; }
    bool fetching() const { return fetching_; }
    bool overwrite() const { return overwrite_; }
    bool confirmingOverwrite() const { return confirm_; }
    int focus() const { return focus_; }
    std::string statusText() const;  // leer = Liste ohne Meldung
    std::string rowText(int row) const;  // rechte Spalte: Autor bzw. „nicht wählbar“
    void requestClose() { closeRequested_ = true; }  // nächster update schließt wie B (Smoke-Tests)

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;

private:
    void clampScroll();

    std::optional<CheatModel> model_;
    std::string id6_, title_;
    bool fileExists_ = false;
    std::optional<Str> fetchBlocked_;
    std::optional<Str> message_;
    bool fetching_ = false, overwrite_ = false, confirm_ = false, closeRequested_ = false;
    int focus_ = 0, scroll_ = 0;
};

}  // namespace nl::ui
