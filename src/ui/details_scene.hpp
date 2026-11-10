#pragma once
// Detailansicht (Spec 3.2): Cover-Platzhalter, Titel, Infozeile, Beschreibung, Knöpfe Starten/Favorit.
// Hält eigene Kopien der Spieltexte: ein Hintergrund-Scan darf die Bibliothek austauschen, während sie offen ist.

#include <string>

#include "ui/scene.hpp"

namespace nl::ui {

class DetailsScene final : public Scene {
public:
    void open(const Game& game, bool favorite, u32 lastPlayedUnix);
    void setFavorite(bool favorite) { favorite_ = favorite; }
    int focusedButton() const { return focus_; }
    const Game* game() const { return hasGame_ ? &game_ : nullptr; }

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    void onEnter() override { focus_ = 0; }

    gfx::Rect buttonRect(const UiContext& ctx, int index) const;

private:
    std::string key_, title_, description_, path_;
    Game game_{};
    bool hasGame_ = false;
    bool favorite_ = false;
    u32 lastPlayed_ = 0;
    int focus_ = 0;
    bool keysSincePointerMove_ = false;
    char info_[128] = "";
    char clock_[8] = "";
    int clockMinute_ = -1;
};

// "N64 · Gen 5 · 1996 · 1 Spieler · zuletzt 09.11.2025"; fehlende Teile entfallen. Datum in UTC.
int detailsInfoLine(const Game& g, u32 lastPlayedUnix, char* out, std::size_t cap);

}  // namespace nl::ui
