#include "ui/main_scene.hpp"

#include <algorithm>
#include <cstdio>

#include "core/text_fold.hpp"
#include "gfx/text.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;

constexpr float kBarPadX = 16.0f;
constexpr float kPillH = 32.0f, kPillY = 12.0f, kMinBarGap = 6.0f;
constexpr float kButtonR = 22.0f;
constexpr float kFocusScale = 0.08f;
constexpr float kToastMs = 2000.0f;
constexpr float kAnimMs = 200.0f;
constexpr float kCoverInset = 4.0f;
constexpr int kTitleLines = 3;

bool contains(Rect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

Rect grow(Rect r, float d) { return Rect{r.x - d, r.y - d, r.w + 2 * d, r.h + 2 * d}; }

const char* barLabel(const BarEntry& e) {
    if (e.kind == BarKind::Favorites) return tr(Str::Favorites);
    if (e.kind == BarKind::Recent) return tr(Str::Recent);
    return consoleInfo(e.console).label;
}

void drawHomeIcon(gfx::DrawList& dl, const gfx::AtlasShapes& s, float cx, float cy, Color c) {
    gfx::fillRect(dl, s, Rect{cx - 7, cy - 1, 14, 10}, c);
    gfx::fillRect(dl, s, Rect{cx - 11, cy - 4, 22, 3}, c);
    gfx::fillRect(dl, s, Rect{cx - 7, cy - 7, 14, 3}, c);
    gfx::fillRect(dl, s, Rect{cx - 3, cy - 10, 6, 3}, c);
}

void drawGearIcon(gfx::DrawList& dl, const gfx::AtlasShapes& s, float cx, float cy, Color c, Color hole) {
    gfx::fillRect(dl, s, Rect{cx - 2.5f, cy - 12, 5, 24}, c);
    gfx::fillRect(dl, s, Rect{cx - 12, cy - 2.5f, 24, 5}, c);
    gfx::circle(dl, s, cx, cy, 9, c);
    gfx::circle(dl, s, cx, cy, 4, hole);
}
}  // namespace

MainScene::MainScene(const Library& lib) : lib_(lib), scratch_(lib.games.size()) {
    rebuildBar();
    int start = 0;
    for (int i = 0; i < barCount_; ++i) {
        if (!gamesFor(lib_, bar_[i], scratch_).empty()) {
            start = i;
            break;
        }
    }
    selectBar(start, 640.0f, 480.0f);
}

std::string MainScene::focusedKey() const {
    const Game* g = focusedGame();
    return g != nullptr ? std::string(g->key) : std::string();
}

void MainScene::setLibrary(const Library& lib, std::string_view focusKey) {
    // Nur Werte merken, keine Zeiger der alten Bibliothek lesen (sie sind hier schon ungültig).
    const BarEntry oldEntry = bar_[barIndex_];
    const int oldIndex = barIndex_;
    games_ = {};

    lib_ = lib;
    scratch_.assign(lib_.games.size(), nullptr);
    if (searchActive_) setQuery(std::string(searchQuery_));  // Treffer neu aus lib_ (Kopie: setQuery überschreibt searchQuery_)
    rebuildBar();
    int index = -1;
    for (int i = 0; i < barCount_ && index < 0; ++i) {
        const bool sameKind = bar_[i].kind == oldEntry.kind;
        if (sameKind && (oldEntry.kind != BarKind::ConsoleEntry || bar_[i].console == oldEntry.console)) index = i;
    }
    if (index < 0) index = oldIndex < barCount_ ? oldIndex : barCount_ - 1;
    selectBar(index, gridWidth_, gridHeight_);
    for (std::size_t i = 0; i < games_.size() && !focusKey.empty(); ++i) {
        if (focusKey == games_[i]->key) {
            grid_.setFocus(static_cast<int>(i));
            scroll_.snap(grid_.targetScroll());
            break;
        }
    }
    infoGame_ = nullptr;  // Infozeile neu aufbauen: alte Zeiger sind ungültig
    infoBuf_[0] = '\0';
    hoverBar_ = -1;
}

void MainScene::showToast(const char* text) {
    std::snprintf(toastBuf_, sizeof toastBuf_, "%s", text);
    toastMs_ = kToastMs;
}

void MainScene::selectBar(int index, float width, float height) {
    if (index < 0 || index >= barCount_) return;
    barIndex_ = index;
    games_ = bar_[index].kind == BarKind::Search ? std::span<const Game* const>(searchResults_)
                                                  : gamesFor(lib_, bar_[index], scratch_);
    const GridMetrics m = gridMetrics(width);
    grid_.reset(static_cast<int>(games_.size()), m, height - m.topBarH - m.infoBarH);
    gridWidth_ = width;
    gridHeight_ = height;
    scroll_.snap(0.0f);
    animFocus_ = -1;
    bucketMask_ = 0;
    for (const Game* g : games_) {
        const char b = letterBucket(g->title);
        bucketMask_ |= 1u << (b == '#' ? 0 : b - 'A' + 1);
    }
}

void MainScene::rebuildBar() {
    int offset = 0;
    if (searchActive_) {
        bar_[0] = BarEntry{BarKind::Search, Console::Nes, 0};
        offset = 1;
    }
    barCount_ = offset + buildBarEntries(lib_, std::span<BarEntry>(bar_ + offset, kMaxBarEntries));
}

const char* MainScene::barLabelText(int index) const {
    if (index < 0 || index >= barCount_) return "";
    if (bar_[index].kind == BarKind::Search) return searchLabel_;
    return barLabel(bar_[index]);
}

void MainScene::setQuery(std::string_view query) {
    const std::string folded = foldForSearch(query);
    if (folded.empty()) {
        if (!searchActive_) return;
        searchActive_ = false;
        searchResults_.clear();
        rebuildBar();
        int index = 0;
        for (int i = 0; i < barCount_; ++i) {
            const bool same = bar_[i].kind == entryBeforeSearch_.kind &&
                              (entryBeforeSearch_.kind != BarKind::ConsoleEntry || bar_[i].console == entryBeforeSearch_.console);
            if (same) {
                index = i;
                break;
            }
        }
        selectBar(index, gridWidth_, gridHeight_);
        return;
    }
    if (!searchActive_) {
        entryBeforeSearch_ = bar_[barIndex_];
        searchActive_ = true;
    }
    searchQuery_.assign(query);
    std::snprintf(searchLabel_, sizeof searchLabel_, "%s: %s", tr(Str::Search), searchQuery_.c_str());
    searchResults_.clear();
    for (const Game& g : lib_.games)
        if (matchesQuery(foldForSearch(g.title), folded)) searchResults_.push_back(&g);
    std::stable_sort(searchResults_.begin(), searchResults_.end(),
                     [](const Game* a, const Game* b) { return compareTitles(a->title, b->title) < 0; });
    rebuildBar();
    selectBar(0, gridWidth_, gridHeight_);
    infoGame_ = nullptr;
}

void MainScene::jumpToBucket(char bucket) {
    const int i = firstIndexForBucket(games_, bucket);
    if (i >= 0) grid_.setFocus(i);
}

bool MainScene::hasBucket(char bucket) const {
    const int bit = bucket == '#' ? 0 : bucket - 'A' + 1;
    return bit >= 0 && bit < 27 && (bucketMask_ & (1u << bit)) != 0;
}

char MainScene::currentBucket() const {
    const Game* g = focusedGame();
    return g != nullptr ? letterBucket(g->title) : '#';
}

const Game* MainScene::focusedGame() const {
    const int f = grid_.focus();
    return (f >= 0 && f < static_cast<int>(games_.size())) ? games_[f] : nullptr;
}

const char* MainScene::infoTitle() const {
    const Game* g = focusedGame();
    return g != nullptr ? g->title : dateBuf_;
}

const char* MainScene::infoLineText() const { return toastMs_ > 0.0f ? toastBuf_ : infoBuf_; }

Rect MainScene::homeRect(const UiContext& ctx) const {
    return Rect{kBarPadX, ctx.height - 36 - kButtonR, 2 * kButtonR, 2 * kButtonR};
}

Rect MainScene::gearRect(const UiContext& ctx) const {
    return Rect{ctx.width - kBarPadX - 2 * kButtonR, ctx.height - 36 - kButtonR, 2 * kButtonR, 2 * kButtonR};
}

Rect MainScene::barRect(int index) const {
    if (index < 0 || index >= barCount_) return Rect{0, 0, 0, 0};
    Rect r = barRects_[index];
    r.x -= barScroll_.value();
    return r;
}

void MainScene::layoutBar(const UiContext& ctx) {
    // Einträge bündig mit den Rasterrändern und gleichmäßig über die Breite verteilt; reicht der
    // Platz nicht, rücken sie auf den Mindestabstand zusammen und die Leiste scrollt.
    const gfx::FontFace& pill = ctx.fonts->face(FontSize::Body, FontWeight::Medium);
    const float margin = gridMetrics(ctx.width).marginX;
    float widths[kMaxBarEntries];
    float sum = 0.0f;
    for (int i = 0; i < barCount_; ++i) {
        widths[i] = gfx::measureText(pill, barLabelText(i)) + 24.0f;
        sum += widths[i];
    }
    float gap = barCount_ > 1 ? (ctx.width - 2.0f * margin - sum) / static_cast<float>(barCount_ - 1) : 0.0f;
    if (gap < kMinBarGap) gap = kMinBarGap;
    float x = margin;
    for (int i = 0; i < barCount_; ++i) {
        barRects_[i] = Rect{x, kPillY, widths[i], kPillH};
        x += widths[i] + gap;
    }

    // Aktiven Eintrag in den sichtbaren Bereich scrollen.
    const Rect a = barRects_[barIndex_];
    float target = barScroll_.target();
    if (a.x + a.w > ctx.width - margin + target) target = a.x + a.w - (ctx.width - margin);
    if (a.x - margin < target) target = a.x - margin;
    if (target < 0.0f) target = 0.0f;
    if (target != barScroll_.target()) barScroll_.to(target, kAnimMs);
}

void MainScene::refreshTexts(const UiContext& ctx) {
    if (ctx.minuteOfDay != clockMinute_) {
        clockMinute_ = ctx.minuteOfDay;
        formatClock(clockMinute_, clockBuf_, sizeof clockBuf_);
    }
    const int dateKey = (ctx.year * 13 + ctx.month) * 32 + ctx.day;
    if (dateKey != dateKey_) {
        dateKey_ = dateKey;
        formatDate(ctx.weekday, ctx.day, ctx.month, ctx.year, dateBuf_, sizeof dateBuf_);
    }
    const Game* g = focusedGame();
    if (g != infoGame_) {
        infoGame_ = g;
        if (g != nullptr) {
            infoLine(*g, infoBuf_, sizeof infoBuf_);
        } else {
            infoBuf_[0] = '\0';
        }
    }
}

void MainScene::handlePointer(const UiContext& ctx, const InputFrame& in) {
    if (!in.pointerValid) {
        hover_ = Hover::None;
        hoverBar_ = -1;
        pointerOnTile_ = false;
        return;
    }
    // Hover folgt jeden Frame der aktuellen Position (auch nach einem Overlay ohne Bewegung);
    // der Rasterfokus ändert sich dagegen nur bei echter Bewegung.
    const float x = in.pointerX, y = in.pointerY;
    hover_ = Hover::None;
    hoverBar_ = -1;
    if (contains(homeRect(ctx), x, y)) {
        hover_ = Hover::Home;
    } else if (contains(gearRect(ctx), x, y)) {
        hover_ = Hover::Gear;
    } else if (y < grid_.viewTop()) {
        for (int i = 0; i < barCount_; ++i) {
            if (contains(barRect(i), x, y)) {
                hover_ = Hover::Bar;
                hoverBar_ = i;
                break;
            }
        }
    } else if (in.pointerMoved && y < grid_.viewTop() + grid_.viewHeight()) {
        // Leere Fläche löscht den Fokus nur, wenn der Zeiger vorher eine Kachel fokussiert hatte;
        // ein Zeiger, der irgendwo neben dem Raster auftaucht, nimmt den Fokus nicht weg.
        const int hit = grid_.hitTest(x, y, scroll_.value());
        if (hit >= 0 || pointerOnTile_) grid_.setFocus(hit);
        pointerOnTile_ = hit >= 0;
        return;
    }
    if (in.pointerMoved) pointerOnTile_ = false;
}

void MainScene::update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) {
    if (ctx.width != gridWidth_ || ctx.height != gridHeight_) {
        const int focus = grid_.focus();
        selectBar(barIndex_, ctx.width, ctx.height);
        grid_.setFocus(focus);
        scroll_.snap(grid_.targetScroll());
    }
    if (!modeKnown_ || ctx.themeMode != lastMode_) {
        if (modeKnown_) {
            std::snprintf(toastBuf_, sizeof toastBuf_, "%s: %s", tr(Str::Appearance), tr(ctx.themeMode == ThemeMode::Light ? Str::ThemeLight : ctx.themeMode == ThemeMode::Dark ? Str::ThemeDark : Str::ThemeAuto));
            toastMs_ = kToastMs;
        }
        modeKnown_ = true;
        lastMode_ = ctx.themeMode;
    } else if (toastMs_ > 0.0f) {
        toastMs_ -= dtMs;
    }

    const int focusBefore = grid_.focus(), barBefore = barIndex_;
    layoutBar(ctx);
    handlePointer(ctx, in);

    if (in.pressed(Action::PrevConsole) && barIndex_ > 0) selectBar(barIndex_ - 1, ctx.width, ctx.height);
    if (in.pressed(Action::NextConsole) && barIndex_ + 1 < barCount_) selectBar(barIndex_ + 1, ctx.width, ctx.height);
    if (in.pressed(Action::Left)) grid_.move(-1, 0);
    if (in.pressed(Action::Right)) grid_.move(1, 0);
    if (in.pressed(Action::Up)) grid_.move(0, -1);
    if (in.pressed(Action::Down)) grid_.move(0, 1);

    if (in.pressed(Action::Home) && home_ != nullptr) out.push = home_;
    if (in.pressed(Action::Search) && jumpBar_ != nullptr) out.push = jumpBar_;
    if (in.pressed(Action::Back) && searchActive_) {
        setQuery("");
        out.sound = UiSound::Back;
    }
    if (in.pressed(Action::Favorite)) {
        if (const Game* g = focusedGame()) {
            out.command = AppCommand::ToggleFavorite;
            out.game = g;
        }
    }
    if (in.pressed(Action::Confirm)) {
        if (hover_ == Hover::Home && home_ != nullptr) {
            out.push = home_;
        } else if (hover_ == Hover::Gear) {
            out.command = AppCommand::CycleTheme;
        } else if (hover_ == Hover::Bar && hoverBar_ >= 0) {
            selectBar(hoverBar_, ctx.width, ctx.height);
            out.sound = UiSound::Confirm;
        } else if (const Game* g = focusedGame(); g != nullptr && details_ != nullptr) {
            const bool fav = detailsStore_ != nullptr && detailsStore_->isFavorite(g->key);
            const u32 last = detailsStore_ != nullptr ? detailsStore_->lastPlayed(g->key) : 0;
            details_->open(*g, fav, last);
            if (detailsStore_ != nullptr)
                details_->setDeviceLine(detailsDeviceLine(detailsStore_->devices(), g->device, detailsStore_->alsoOn(*g)));
            out.push = details_;
        }
    }
    deriveSound(out, (grid_.focus() != focusBefore && grid_.focus() >= 0) || barIndex_ != barBefore);

    layoutBar(ctx);
    if (grid_.targetScroll() != scroll_.target()) scroll_.to(grid_.targetScroll(), kAnimMs);
    if (grid_.focus() != animFocus_) {
        animFocus_ = grid_.focus();
        focusAnim_.snap(0.0f);
        focusAnim_.to(1.0f, kAnimMs);
    }
    scroll_.update(dtMs);
    barScroll_.update(dtMs);
    focusAnim_.update(dtMs);
    refreshTexts(ctx);
    requestCovers();
}

void MainScene::requestCovers() {
    if (covers_ == nullptr) return;
    const float scroll = scroll_.value();
    const float top = grid_.viewTop(), bottom = top + grid_.viewHeight();
    const float ahead = 2.0f * grid_.rowPitch();
    const int n = std::min(grid_.count(), static_cast<int>(games_.size()));
    for (int i = 0; i < n; ++i) {
        const Rect r = grid_.tileRect(i, scroll);
        if (r.y + r.h < top - ahead) continue;
        if (r.y > bottom + ahead) break;  // Kacheln sind zeilenweise sortiert
        const bool visible = r.y + r.h >= top && r.y <= bottom;
        covers_->want(*games_[i], visible ? CoverPriority::Visible : CoverPriority::Prefetch);
    }
}

void MainScene::drawTile(const UiContext& ctx, gfx::DrawList& dl, int index, Rect r, bool focused) const {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    if (focused) gfx::roundRect(dl, s, grow(r, 3), 13, t.accent);
    gfx::roundRect(dl, s, r, 10, focused ? t.tileFocus : t.tile);
    if (index < 0 || index >= static_cast<int>(games_.size())) return;
    const Game& g = *games_[index];

    // Platzhalter (Titel) zuerst: das Cover blendet darüber ein, Tag und Favoritenpunkt liegen obenauf.
    const gfx::FontFace& body = ctx.fonts->face(FontSize::Small, FontWeight::Regular);
    const float maxW = r.w - 16.0f;
    gfx::Line lines[kTitleLines];
    const std::string_view title(g.title);
    const int n = gfx::wrapText(body, title, maxW, lines);
    float baseline = r.y + 34.0f + body.ascent;
    for (int i = 0; i < n; ++i) {
        const std::string_view line = title.substr(lines[i].start, lines[i].length);
        if (baseline + body.descent > r.y + r.h) break;
        gfx::drawTextEllipsis(dl, body, r.x + 8, baseline, maxW, line, t.text);
        baseline += body.lineHeight;
    }

    if (covers_ != nullptr) {
        const CoverView cv = covers_->view(g);
        if (cv.texture != 0 && cv.alpha > 0.0f) {
            const u8 a = static_cast<u8>(std::clamp(cv.alpha, 0.0f, 1.0f) * 255.0f);
            dl.quad(cv.texture, fitCoverRect(r, kCoverInset), gfx::UvRect{0, 0, 1, 1}, Color{255, 255, 255, a});
        }
    }

    const gfx::FontFace& label = ctx.fonts->face(FontSize::Small, FontWeight::Medium);
    const char* tag = consoleInfo(g.console).label;
    const float tagW = gfx::measureText(label, tag) + 12.0f;
    gfx::roundRect(dl, s, Rect{r.x + 8, r.y + 8, tagW, 18}, 9, t.accentSoft);
    if (store_ != nullptr && store_->isFavorite(g.key)) gfx::circle(dl, s, r.x + r.w - 14, r.y + 17, 4, t.accent);
    gfx::drawText(dl, label, r.x + 14, r.y + 8 + 9 + (label.ascent - label.descent) / 2, tag, t.accentSoftText);
}

void MainScene::drawTopBar(const UiContext& ctx, gfx::DrawList& dl) const {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    const float barH = grid_.viewTop();
    gfx::fillRect(dl, s, Rect{0, 0, ctx.width, barH}, t.bar);
    gfx::fillRect(dl, s, Rect{0, barH - 1, ctx.width, 1}, t.barBorder);

    const gfx::FontFace& pill = ctx.fonts->face(FontSize::Body, FontWeight::Medium);
    for (int i = 0; i < barCount_; ++i) {
        const Rect r = barRect(i);
        if (r.x > ctx.width || r.x + r.w < 0) continue;
        Color text = t.text2;
        if (i == barIndex_) {
            gfx::roundRect(dl, s, r, kPillH / 2, t.accent);
            text = t.onAccent;
        } else if (i == hoverBar_) {
            gfx::roundRect(dl, s, r, kPillH / 2, t.accentSoft);
            text = t.accentSoftText;
        }
        gfx::drawText(dl, pill, r.x + 12, r.y + 16 + (pill.ascent - pill.descent) / 2, barLabelText(i), text);
    }
}

void MainScene::drawInfoBar(const UiContext& ctx, gfx::DrawList& dl) const {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    const float top = ctx.height - 72.0f;
    gfx::fillRect(dl, s, Rect{0, top, ctx.width, 72}, t.bar);
    gfx::fillRect(dl, s, Rect{0, top, ctx.width, 1}, t.barBorder);

    const Rect home = homeRect(ctx), gear = gearRect(ctx);
    gfx::circle(dl, s, home.x + kButtonR, home.y + kButtonR, kButtonR, hover_ == Hover::Home ? t.accentSoft : t.bg);
    drawHomeIcon(dl, s, home.x + kButtonR, home.y + kButtonR, t.text);
    const Color gearBg = hover_ == Hover::Gear ? t.accentSoft : t.bg;
    gfx::circle(dl, s, gear.x + kButtonR, gear.y + kButtonR, kButtonR, gearBg);
    drawGearIcon(dl, s, gear.x + kButtonR, gear.y + kButtonR, t.text, gearBg);

    const gfx::FontFace& clock = ctx.fonts->face(FontSize::Body, FontWeight::Medium);
    const float clockW = gfx::measureText(clock, clockBuf_);
    const float clockX = gear.x - 12.0f - clockW;
    gfx::drawText(dl, clock, clockX, top + 36 + (clock.ascent - clock.descent) / 2, clockBuf_, t.text2);

    const float textX = home.x + home.w + 16.0f;
    const float maxW = clockX - 16.0f - textX;
    const gfx::FontFace& title = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    const gfx::FontFace& body = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    const char* line = infoLineText();
    if (line[0] == '\0') {
        gfx::drawTextEllipsis(dl, title, textX, top + 36 + (title.ascent - title.descent) / 2, maxW, infoTitle(), t.text);
    } else {
        gfx::drawTextEllipsis(dl, title, textX, top + 30, maxW, infoTitle(), t.text);
        gfx::drawTextEllipsis(dl, body, textX, top + 56, maxW, line, t.text2);
    }
}

void MainScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    gfx::fillRect(dl, *ctx.shapes, Rect{0, 0, ctx.width, ctx.height}, t.bg);
    if (ctx.background)  // Hintergrundbild unter allem, auf den ganzen Bildschirm gestreckt
        dl.quad(gfx::kBackgroundTexture, Rect{0, 0, ctx.width, ctx.height},
                gfx::UvRect{0.0f, 0.0f, ctx.backgroundU1, ctx.backgroundV1}, Color{255, 255, 255, 255});

    const float scroll = scroll_.value();
    const float viewTop = grid_.viewTop(), viewBottom = viewTop + grid_.viewHeight();
    const int focus = grid_.focus();
    for (int i = 0; i < grid_.count(); ++i) {
        if (i == focus) continue;
        const Rect r = grid_.tileRect(i, scroll);
        if (r.y + r.h < viewTop || r.y > viewBottom) continue;
        drawTile(ctx, dl, i, r, false);
    }
    if (focus >= 0) {
        const Rect r = grid_.tileRect(focus, scroll);
        const float k = kFocusScale * focusAnim_.value();
        const Rect scaled{r.x - r.w * k / 2, r.y - r.h * k / 2, r.w * (1 + k), r.h * (1 + k)};
        drawTile(ctx, dl, focus, scaled, true);
    }
    if (grid_.count() == 0) {
        const gfx::FontFace& f = ctx.fonts->face(FontSize::Title, FontWeight::Regular);
        const char* empty = searchActive_ && bar_[barIndex_].kind == BarKind::Search ? tr(Str::NoResults) : tr(Str::NoGames);
        const float w = gfx::measureText(f, empty);
        gfx::drawText(dl, f, (ctx.width - w) / 2, viewTop + grid_.viewHeight() / 2, empty, t.textMuted);
    }

    drawTopBar(ctx, dl);
    drawInfoBar(ctx, dl);
}

}  // namespace nl::ui
