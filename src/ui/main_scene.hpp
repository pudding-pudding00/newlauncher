#pragma once
// Hauptbildschirm (Spec 3.1, Layout A): Konsolenleiste oben, Cover-Raster, Infoleiste unten.

#include <string>
#include <vector>

#include "core/categories.hpp"
#include "core/easing.hpp"
#include "core/game_filter.hpp"
#include "core/library_store.hpp"
#include "core/play_stats.hpp"
#include "ui/cover_provider.hpp"
#include "ui/details_scene.hpp"
#include "ui/jump_bar.hpp"
#include "ui/keyboard.hpp"
#include "ui/layout.hpp"
#include "ui/scene.hpp"

namespace nl::ui {

class MainScene final : public Scene, public JumpTarget, public SearchTarget {
public:
    explicit MainScene(const Library& lib);

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    void setHomeMenu(Scene* home) { home_ = home; }
    // Neue Bibliothek (z. B. nach dem Hintergrund-Scan): Leisten-Eintrag und Fokus bleiben per
    // Art/Konsole bzw. Spiel-Schlüssel erhalten, wenn möglich. focusKey muss VOR der Änderung am
    // LibraryStore mit focusedKey() geholt werden: danach zeigen die alten Game-Zeiger ins Leere,
    // und setLibrary liest sie deshalb nicht mehr.
    void setLibrary(const Library& lib, std::string_view focusKey);
    std::string focusedKey() const;
    void showToast(const char* text);                     // ersetzt die Infozeile für 2 s
    void setFavoriteKeys(const LibraryStore* store) { store_ = store; }  // für die Favoriten-Markierung
    void setJumpBar(Scene* jumpBar) { jumpBar_ = jumpBar; }
    // nullptr = immer Platzhalter. update() fordert sichtbare Kacheln und zwei Reihen darüber/darunter an.
    void setCovers(CoverProvider* covers) { covers_ = covers; }
    void setKeyboard(Scene* keyboard) { keyboard_ = keyboard; }
    // Confirm auf fokussierter Kachel öffnet die Detailansicht; store liefert Favorit und Verlauf (darf nullptr sein).
    void setDetails(DetailsScene* details, const LibraryStore* store) {
        details_ = details;
        detailsStore_ = store;
    }

    // JumpTarget
    void jumpToBucket(char bucket) override;
    Scene* openSearch() override { return keyboard_; }
    bool hasBucket(char bucket) const override;
    char currentBucket() const override;
    // Filterzeile (M17): Alle, Favoriten, vorhandene Genres, Kategorien. Gilt je Leisten-Eintrag;
    // ein Wechsel des Eintrags setzt auf Alle zurück.
    int filterCount() const override { return static_cast<int>(choices_.size()); }
    int filterIndex() const override;
    std::string filterLabel(int index) const override;
    void setFilterIndex(int index) override;
    void setCategories(const Categories* cats);  // nullptr = keine Kategorien
    int setFilter(const GameFilter& f);          // für [debug] filter; liefert die Trefferzahl
    void refreshFilter();                        // nach Änderung an Kategorien/Favoriten; Fokus bleibt am Spiel
    void syncDetailsCategories(std::string_view key);  // Kategorienzeile der Detailansicht für dieses Spiel
    const GameFilter& filter() const { return filter_; }
    void selectBarForTest(int index) { selectBar(index, gridWidth_, gridHeight_); }
    // Sortierung der Konsolenleisten (M17); Favoriten, Verlauf und Suche behalten ihre Reihenfolge.
    // Wirkt sofort; stats == nullptr = keine Statistik. Der Fokus bleibt am Spiel.
    void setSort(SortOrder order, const PlayStats* stats);

    // SearchTarget: nicht leere Anfrage → Such-Eintrag vorn in der Leiste mit Treffern aus allen Konsolen;
    // leere Anfrage → Suchmodus aus, vorheriger Eintrag wieder aktiv.
    void setQuery(std::string_view query) override;
    bool searchActive() const { return searchActive_; }
    const char* barLabelText(int index) const;
    void setFocusForTest(int index) { grid_.setFocus(index); }

    // Für Tests und Selbsttest
    int barIndex() const { return barIndex_; }
    int focusIndex() const { return grid_.focus(); }
    const GridModel& grid() const { return grid_; }
    const char* infoTitle() const;
    const char* infoLineText() const;
    gfx::Rect homeRect(const UiContext& ctx) const;
    gfx::Rect gearRect(const UiContext& ctx) const;
    gfx::Rect barRect(int index) const;
    void drawTile(const UiContext& ctx, gfx::DrawList& dl, int index, gfx::Rect r, bool focused) const;

private:
    enum class Hover : u8 { None, Home, Gear, Bar };

    void selectBar(int index, float width, float height);
    void applyCurrentFilter(std::string_view keepKey);
    void layoutBar(const UiContext& ctx);
    void refreshTexts(const UiContext& ctx);
    void handlePointer(const UiContext& ctx, const InputFrame& in);
    void drawTopBar(const UiContext& ctx, gfx::DrawList& dl) const;
    void drawInfoBar(const UiContext& ctx, gfx::DrawList& dl) const;
    const Game* focusedGame() const;
    void requestCovers();

    Library lib_;
    void rebuildBar();
    BarEntry bar_[kMaxBarEntries + 1]{};  // + Such-Eintrag
    int barCount_ = 0, barIndex_ = 0;
    gfx::Rect barRects_[kMaxBarEntries + 1]{};
    std::vector<const Game*> scratch_;
    std::span<const Game* const> games_;  // gefilterte Liste (zeigt in filtered_)
    std::span<const Game* const> base_;   // ungefilterte Liste des Leisten-Eintrags
    std::vector<const Game*> filtered_;
    std::vector<GameFilter> choices_;
    GameFilter filter_;
    const Categories* cats_ = nullptr;
    SortOrder sort_ = SortOrder::Title;
    const PlayStats* playStats_ = nullptr;
    void loadBase(int index);  // base_ für den Leisten-Eintrag, Konsolen sortiert
    BarEntry filterEntry_{};
    bool filterEntryKnown_ = false;
    GridModel grid_;
    float gridWidth_ = 0.0f, gridHeight_ = 0.0f;
    Tween scroll_, barScroll_, focusAnim_;
    int animFocus_ = -1;

    Hover hover_ = Hover::None;
    int hoverBar_ = -1;
    bool pointerOnTile_ = false;
    Scene* home_ = nullptr;
    const LibraryStore* store_ = nullptr;
    Scene* jumpBar_ = nullptr;
    Scene* keyboard_ = nullptr;
    DetailsScene* details_ = nullptr;
    CoverProvider* covers_ = nullptr;
    const LibraryStore* detailsStore_ = nullptr;
    u32 bucketMask_ = 0;
    bool searchActive_ = false;
    BarEntry entryBeforeSearch_{};
    std::vector<const Game*> searchResults_;
    std::string searchQuery_;
    char searchLabel_[64] = "";  // Bit 0 = '#', Bit 1–26 = A–Z; je Liste vorberechnet

    char clockBuf_[8] = "";
    int clockMinute_ = -1;
    char dateBuf_[64] = "";
    int dateKey_ = -1;
    char infoBuf_[64] = "";
    const Game* infoGame_ = nullptr;
    char toastBuf_[64] = "";
    float toastMs_ = 0.0f;
    ThemeMode lastMode_ = ThemeMode::Auto;
    bool modeKnown_ = false;
};

}  // namespace nl::ui
