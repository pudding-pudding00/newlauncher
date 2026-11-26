#pragma once
// Detailansicht (Spec 3.2): Cover (sonst Platzhalter), Titel, Infozeile, Beschreibung, Knöpfe Starten/Favorit.
// Hält eigene Kopien der Spieltexte: ein Hintergrund-Scan darf die Bibliothek austauschen, während sie offen ist.

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/n64_choice.hpp"
#include "core/play_stats.hpp"
#include "ui/banner_provider.hpp"
#include "ui/cover_provider.hpp"
#include "ui/scene.hpp"

namespace nl::ui {

class DetailsScene final : public Scene {
public:
    void open(const Game& game, bool favorite, u32 lastPlayedUnix);
    void setFavorite(bool favorite) { favorite_ = favorite; }
    // Statistik (M17): bei starts > 0 eigene Zeile „12-mal gestartet, zuletzt am …“; die Infozeile verliert
    // dann ihr Verlaufsdatum. open() setzt sie zurück.
    void setStats(PlayStat s);
    const char* infoText() const { return info_; }
    const char* statsText() const { return stats_; }
    void setCovers(CoverProvider* covers) { covers_ = covers; }  // nullptr = Platzhalter
    void setBanners(BannerProvider* banners) { banners_ = banners; }  // nullptr = kein Banner (M18)
    // N64: Zeile „Startmethode“ (Spec 6.6). Wirkt nur bei geöffnetem N64-Spiel; open() setzt sie zurück.
    void setN64(const N64Decision& decision, std::optional<N64Method> override_);
    int methodChoice() const { return choice_; }  // 0 = Automatisch, sonst 1 + N64Method
    static constexpr int kFocusMethod = 2;
    static constexpr int kFocusCheats = 3;  // Zeile „Cheats“ über den Knöpfen (Wii, GC; M15)
    static constexpr int kFocusCategories = 4;  // Zeile „Kategorien“ direkt über den Knöpfen (M17)
    static constexpr int kFocusSave = 5;  // Zeile „Spielstand“ rechts neben „Cheats“ bzw. der Startmethode (M26)
    // Spielstand-Zeile fürs geöffnete Spiel ein/aus (main kennt den Speicherort); open() setzt sie zurück.
    void setHasSave(bool hasSave);
    // Kategorien mit Häkchen fürs geöffnete Spiel; leer = keine Zeile. Die Auswahl bleibt, solange sie gültig ist.
    void setCategories(std::vector<std::string> names, std::vector<bool> checked);
    int categoryIndex() const { return catIndex_; }
    bool categoryChecked(int i) const {
        return i >= 0 && i < static_cast<int>(catChecked_.size()) && catChecked_[static_cast<std::size_t>(i)];
    }
    int focusedButton() const { return focus_; }
    void setDeviceLine(std::string line) { device_ = std::move(line); }  // nach open(); leer = keine Zeile
    const std::string& deviceLine() const { return device_; }
    const Game* game() const { return hasGame_ ? &game_ : nullptr; }

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    void onEnter() override { focus_ = 0; }

    gfx::Rect buttonRect(const UiContext& ctx, int index) const;
    gfx::Rect saveRect(const UiContext& ctx) const;

private:
    std::string key_, title_, description_, path_, device_;
    Game game_{};
    bool hasGame_ = false;
    CoverProvider* covers_ = nullptr;
    BannerProvider* banners_ = nullptr;
    bool favorite_ = false;
    u32 lastPlayed_ = 0;
    int focus_ = 0;
    bool keysSincePointerMove_ = false;
    char info_[128] = "";
    char stats_[96] = "";
    char clock_[8] = "";
    int clockMinute_ = -1;
    bool hasN64_ = false;
    bool hasCheats_ = false;  // Wii/GC
    bool hasSave_ = false;    // M26
    N64Decision n64_{};
    int choice_ = 0;
    char method_[96] = "";
    char hint_[256] = "";
    std::vector<std::string> catNames_;
    std::vector<bool> catChecked_;
    int catIndex_ = 0;

    bool choiceAllowed(int c) const;
    gfx::Rect cheatsRect(const UiContext& ctx) const;  // auch Zeile der N64-Startmethode
    gfx::Rect categoriesRect(const UiContext& ctx) const;
    bool hasCategories() const { return !catNames_.empty(); }
    int upperRowFocus() const;  // Cheats bzw. Startmethode bzw. Spielstand, sonst -1
    void refreshN64Text();
};

// "Gerät: USB 1 (auch auf: SD)" (Spec v2 5.2); leer, wenn die Sitzung nur ein Gerät hat.
std::string detailsDeviceLine(std::span<const std::string> devices, u8 device, std::span<const u8> alsoOn);

// "N64 · Gen 5 · 1996 · 1 Spieler · zuletzt 09.11.2025"; fehlende Teile entfallen. Datum in UTC.
int detailsInfoLine(const Game& g, u32 lastPlayedUnix, char* out, std::size_t cap);

// Datum in UTC je Sprache: DE "03.11.2026", EN "2026-11-03".
int formatDate(u32 unixTime, char* out, std::size_t cap);
// "12-mal gestartet, zuletzt am 03.11.2026" / "Started once, last on 2026-11-03"; ohne Zeit ohne Datum;
// starts == 0 → leer.
int detailsStatsLine(PlayStat s, char* out, std::size_t cap);

// Gründe für nicht verfügbare Wege, z. B. "VC-Inject: nicht zugeordnet · Wii64: DOL fehlt";
// bei ignoriertem Override mit tr(Str::OverrideIgnored) davor. Leer, wenn alles verfügbar ist.
int n64HintLine(const N64Decision& d, char* out, std::size_t cap);

}  // namespace nl::ui
