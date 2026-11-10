#pragma once
// Detailansicht (Spec 3.2): Cover (sonst Platzhalter), Titel, Infozeile, Beschreibung, Knöpfe Starten/Favorit.
// Hält eigene Kopien der Spieltexte: ein Hintergrund-Scan darf die Bibliothek austauschen, während sie offen ist.

#include <optional>
#include <string>

#include "core/n64_choice.hpp"
#include "ui/cover_provider.hpp"
#include "ui/scene.hpp"

namespace nl::ui {

class DetailsScene final : public Scene {
public:
    void open(const Game& game, bool favorite, u32 lastPlayedUnix);
    void setFavorite(bool favorite) { favorite_ = favorite; }
    void setCovers(CoverProvider* covers) { covers_ = covers; }  // nullptr = Platzhalter
    // N64: Zeile „Startmethode“ (Spec 6.6). Wirkt nur bei geöffnetem N64-Spiel; open() setzt sie zurück.
    void setN64(const N64Decision& decision, std::optional<N64Method> override_);
    int methodChoice() const { return choice_; }  // 0 = Automatisch, sonst 1 + N64Method
    static constexpr int kFocusMethod = 2;
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
    CoverProvider* covers_ = nullptr;
    bool favorite_ = false;
    u32 lastPlayed_ = 0;
    int focus_ = 0;
    bool keysSincePointerMove_ = false;
    char info_[128] = "";
    char clock_[8] = "";
    int clockMinute_ = -1;
    bool hasN64_ = false;
    N64Decision n64_{};
    int choice_ = 0;
    char method_[96] = "";
    char hint_[256] = "";

    bool choiceAllowed(int c) const;
    void refreshN64Text();
};

// "N64 · Gen 5 · 1996 · 1 Spieler · zuletzt 09.11.2025"; fehlende Teile entfallen. Datum in UTC.
int detailsInfoLine(const Game& g, u32 lastPlayedUnix, char* out, std::size_t cap);

// Gründe für nicht verfügbare Wege, z. B. "VC-Inject: nicht zugeordnet · Wii64: DOL fehlt";
// bei ignoriertem Override mit kStrOverrideIgnored davor. Leer, wenn alles verfügbar ist.
int n64HintLine(const N64Decision& d, char* out, std::size_t cap);

}  // namespace nl::ui
