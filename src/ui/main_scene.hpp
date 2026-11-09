#pragma once
// Hauptbildschirm (Spec 3.1, Layout A): Konsolenleiste oben, Cover-Raster, Infoleiste unten.

#include <string>
#include <vector>

#include "core/easing.hpp"
#include "core/library_store.hpp"
#include "ui/jump_bar.hpp"
#include "ui/layout.hpp"
#include "ui/scene.hpp"

namespace nl::ui {

class MainScene final : public Scene, public JumpTarget {
public:
    explicit MainScene(const Library& lib);

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    void setHomeMenu(Scene* home) { home_ = home; }
    // Neue Bibliothek (z. B. nach dem Hintergrund-Scan): Leisten-Eintrag und Fokus bleiben per
    // Art/Konsole bzw. Spiel-Schlüssel erhalten, wenn möglich.
    void setLibrary(const Library& lib);
    void showToast(const char* text);                     // ersetzt die Infozeile für 2 s
    void setFavoriteKeys(const LibraryStore* store) { store_ = store; }  // für die Favoriten-Markierung
    void setJumpBar(Scene* jumpBar) { jumpBar_ = jumpBar; }
    void setKeyboard(Scene* keyboard) { keyboard_ = keyboard; }

    // JumpTarget
    void jumpToBucket(char bucket) override;
    void openSearch() override { pendingSearch_ = true; }  // öffnet die Tastatur im nächsten eigenen Update
    bool hasBucket(char bucket) const override;
    char currentBucket() const override;

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
    void layoutBar(const UiContext& ctx);
    void refreshTexts(const UiContext& ctx);
    void handlePointer(const UiContext& ctx, const InputFrame& in);
    void drawTopBar(const UiContext& ctx, gfx::DrawList& dl) const;
    void drawInfoBar(const UiContext& ctx, gfx::DrawList& dl) const;
    const Game* focusedGame() const;

    Library lib_;
    BarEntry bar_[kMaxBarEntries]{};
    int barCount_ = 0, barIndex_ = 0;
    gfx::Rect barRects_[kMaxBarEntries]{};
    std::vector<const Game*> scratch_;
    std::span<const Game* const> games_;
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
    bool pendingSearch_ = false;
    u32 bucketMask_ = 0;  // Bit 0 = '#', Bit 1–26 = A–Z; je Liste vorberechnet

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
