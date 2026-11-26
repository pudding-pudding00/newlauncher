#include "ui/main_scene.hpp"

#include <algorithm>
#include <cstdio>

#include "core/text_fold.hpp"
#include "gfx/text.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
float infoBarH(const UiContext& ctx) { return gridMetrics(ctx.width).infoBarH; }  // M24: 60 statt 72
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;

constexpr float kBarPadX = 16.0f;
constexpr float kPillH = 32.0f, kPillY = 8.0f, kMinBarGap = 6.0f;
constexpr float kButtonR = 22.0f;
constexpr float kFocusScale = 0.08f;
constexpr float kToastMs = 2000.0f;
constexpr float kAnimMs = 200.0f;
constexpr float kCoverInset = 2.0f;  // M24
constexpr float kEdgeScrollPx = 40.0f, kEdgeRowMs = 250.0f;  // M23: Zeiger-Randstreifen, Tempo am Rand
constexpr int kTitleLines = 3;

bool contains(Rect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

Rect grow(Rect r, float d) { return Rect{r.x - d, r.y - d, r.w + 2 * d, r.h + 2 * d}; }

const char* barLabel(const BarEntry& e) {
    if (e.kind == BarKind::Favorites) return tr(Str::Favorites);
    if (e.kind == BarKind::Recent) return tr(Str::Recent);
    return consoleLabel(e.console);
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

void drawWifiIcon(gfx::DrawList& dl, const gfx::AtlasShapes& s, float x, float bottomY, WifiStatus status,
                  float animMs, const Theme& t) {
    // M25: verbunden 4 Balken; eingeschränkt 2 Balken + gelber Punkt; verbindet: Lauflicht; getrennt: roter Punkt.
    constexpr float kBarW = 4.0f, kGap = 2.0f;
    constexpr float kHeights[4] = {5.0f, 9.0f, 13.0f, 17.0f};
    constexpr Color kWarn{230, 160, 30, 255};
    const int runner = static_cast<int>(animMs / 250.0f) % 5;  // 0..3 Balken an, 4 = Pause
    for (int b = 0; b < 4; ++b) {
        Color c = t.barBorder;
        if (status == WifiStatus::Connected) c = t.accent;
        else if (status == WifiStatus::Limited && b < 2) c = t.accent;
        else if (status == WifiStatus::Connecting && b <= runner && runner < 4) c = t.text2;
        gfx::fillRect(dl, s,
                      Rect{x + static_cast<float>(b) * (kBarW + kGap), bottomY - kHeights[b], kBarW, kHeights[b]}, c);
    }
    if (status == WifiStatus::Disconnected) gfx::circle(dl, s, x + 3.0f, bottomY - 14.0f, 3.0f, t.danger);
    if (status == WifiStatus::Limited) gfx::circle(dl, s, x + 3.0f, bottomY - 14.0f, 3.0f, kWarn);
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
    base_ = {};

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
    const bool sameEntry = filterEntryKnown_ && bar_[index].kind == filterEntry_.kind &&
                           (bar_[index].kind != BarKind::ConsoleEntry || bar_[index].console == filterEntry_.console);
    if (!sameEntry) filter_ = GameFilter{};  // Filter gilt je Leisten-Eintrag
    filterEntry_ = bar_[index];
    filterEntryKnown_ = true;
    barIndex_ = index;
    loadBase(index);
    gridWidth_ = width;
    gridHeight_ = height;
    applyCurrentFilter({});
}

void MainScene::loadBase(int index) {
    if (bar_[index].kind == BarKind::Search) {
        base_ = searchResults_;
        return;
    }
    base_ = gamesFor(lib_, bar_[index], scratch_);
    if (bar_[index].kind == BarKind::ConsoleEntry && sort_ != SortOrder::Title) {
        static const PlayStats kNoStats;
        // Konsolen-Einträge liegen in scratch_ (gamesFor), also dort sortieren.
        sortGames(std::span<const Game*>(scratch_.data(), base_.size()), sort_,
                  playStats_ != nullptr ? *playStats_ : kNoStats);
    }
}

float tagAlpha(bool disc, float coverAlpha) { return disc ? 1.0f : 1.0f - std::clamp(coverAlpha, 0.0f, 1.0f); }

std::string MainScene::sortLabel() const {
    constexpr Str kNames[] = {Str::SortTitle, Str::SortYear, Str::SortRecent, Str::SortMost};
    const int i = static_cast<int>(sort_);
    return std::string(tr(Str::SortOrder)) + ": " + tr(kNames[i >= 0 && i < 4 ? i : 0]);
}

void MainScene::setSort(SortOrder order, const PlayStats* stats) {
    sort_ = order;
    playStats_ = stats;
    if (barIndex_ < 0 || barIndex_ >= barCount_ || bar_[barIndex_].kind != BarKind::ConsoleEntry) return;
    const std::string key = focusedKey();
    loadBase(barIndex_);
    applyCurrentFilter(key);
}

void MainScene::applyCurrentFilter(std::string_view keepKey) {
    static const Categories kNoCategories;
    const Categories& cats = cats_ != nullptr ? *cats_ : kNoCategories;
    const LibraryStore* favs = store_ != nullptr ? store_ : detailsStore_;
    choices_ = filterChoices(base_, cats);
    // Ein Genre ohne Treffer fehlt in filterChoices; es bleibt wählbar, damit die Zeile nicht „Alle“ zeigt.
    if (std::find(choices_.begin(), choices_.end(), filter_) == choices_.end()) choices_.push_back(filter_);
    filtered_ = applyFilter(base_, filter_, favs, cats);
    games_ = filtered_;
    // M23: in einer Konsolenliste folgt die Kachelhöhe dem Cover-Format; gemischte Listen bleiben 5:7.
    const bool consoleList = filterEntryKnown_ && filterEntry_.kind == BarKind::ConsoleEntry;
    const GridMetrics m = gridMetrics(gridWidth_, consoleList ? coverAspect(filterEntry_.console) : 5.0f / 7.0f);
    // M24: gemischte Listen (Favoriten, Zuletzt, Suche) im Cover-Format je Spiel (Variante B).
    tileHeights_.clear();
    if (!consoleList)
        for (const Game* g : games_) tileHeights_.push_back(gridMetrics(gridWidth_, coverAspect(g->console)).tileH);
    grid_.reset(static_cast<int>(games_.size()), m, gridHeight_ - m.topBarH - m.infoBarH, tileHeights_);
    scroll_.snap(0.0f);
    animFocus_ = -1;
    bucketMask_ = 0;
    for (const Game* g : games_) {
        const char b = letterBucket(g->title);
        bucketMask_ |= 1u << (b == '#' ? 0 : b - 'A' + 1);
    }
    for (std::size_t i = 0; i < games_.size() && !keepKey.empty(); ++i) {
        if (keepKey == games_[i]->key) {
            grid_.setFocus(static_cast<int>(i));
            scroll_.snap(grid_.targetScroll());
            break;
        }
    }
    infoGame_ = nullptr;
}

int MainScene::filterIndex() const {
    for (std::size_t i = 0; i < choices_.size(); ++i)
        if (choices_[i] == filter_) return static_cast<int>(i);
    return 0;
}

std::string MainScene::filterLabel(int index) const {
    if (index < 0 || index >= static_cast<int>(choices_.size())) return {};
    const GameFilter& f = choices_[static_cast<std::size_t>(index)];
    switch (f.kind) {
    case GameFilter::All: return tr(Str::FilterAll);
    case GameFilter::Favorites: return tr(Str::Favorites);
    case GameFilter::Genre: return genreLabel(f.genre);
    case GameFilter::Category: return f.category;
    }
    return {};
}

void MainScene::setFilterIndex(int index) {
    if (index < 0 || index >= static_cast<int>(choices_.size())) return;
    filter_ = choices_[static_cast<std::size_t>(index)];
    applyCurrentFilter(focusedKey());
}

void MainScene::setCategories(const Categories* cats) {
    cats_ = cats;
    refreshFilter();
}

int MainScene::setFilter(const GameFilter& f) {
    filter_ = f;
    applyCurrentFilter({});
    return static_cast<int>(games_.size());
}

void MainScene::refreshFilter() { applyCurrentFilter(focusedKey()); }

void MainScene::syncDetailsCategories(std::string_view key) {
    if (details_ == nullptr) return;
    std::vector<std::string> names;
    std::vector<bool> checked;
    if (cats_ != nullptr) {
        for (const std::string& name : cats_->names()) {
            names.push_back(name);
            checked.push_back(cats_->contains(name, key));
        }
    }
    details_->setCategories(std::move(names), std::move(checked));
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
        const GameFilter kept = filter_;  // Tastatur kann noch offen sein: Filter der Zeile bleibt
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
        if (!(filter_ == kept)) {
            filter_ = kept;
            applyCurrentFilter({});
        }
        return;
    }
    const bool starting = !searchActive_;
    if (starting) {
        entryBeforeSearch_ = bar_[barIndex_];
        searchActive_ = true;
    }
    const GameFilter carried = filter_;  // Filter aus dem Overlay gilt auch für die Treffer
    searchQuery_.assign(query);
    std::snprintf(searchLabel_, sizeof searchLabel_, "%s: %s", tr(Str::Search), searchQuery_.c_str());
    searchResults_.clear();
    for (const Game& g : lib_.games)
        if (matchesQuery(foldForSearch(g.title), folded)) searchResults_.push_back(&g);
    std::stable_sort(searchResults_.begin(), searchResults_.end(),
                     [](const Game* a, const Game* b) { return compareTitles(a->title, b->title) < 0; });
    rebuildBar();
    selectBar(0, gridWidth_, gridHeight_);
    if (starting && !(filter_ == carried)) {
        filter_ = carried;
        applyCurrentFilter({});
    }
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
    return Rect{kBarPadX, ctx.height - infoBarH(ctx) / 2 - kButtonR, 2 * kButtonR, 2 * kButtonR};
}

Rect MainScene::gearRect(const UiContext& ctx) const {
    return Rect{ctx.width - kBarPadX - 2 * kButtonR, ctx.height - infoBarH(ctx) / 2 - kButtonR, 2 * kButtonR,
                2 * kButtonR};
}

Rect MainScene::wifiRect(const UiContext& ctx) const {
    const float barH = infoBarH(ctx), top = ctx.height - barH;
    const Rect gear = gearRect(ctx);
    const gfx::FontFace& clock = ctx.fonts->face(FontSize::Body, FontWeight::Medium);
    const float clockW = gfx::measureText(clock, clockBuf_[0] != '\0' ? clockBuf_ : "00:00");
    const float clockX = gear.x - 12.0f - clockW;
    constexpr float kWifiW = 42.0f, kWifiH = 44.0f;
    return Rect{clockX - 12.0f - kWifiW, top + (barH - kWifiH) / 2.0f, kWifiW, kWifiH};
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
    float widths[kMaxBarEntries + 1];
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

void MainScene::handlePointer(const UiContext& ctx, const InputFrame& in, float dtMs) {
    if (!in.pointerValid) {
        hover_ = Hover::None;
        hoverBar_ = -1;
        pointerOnTile_ = false;
        return;
    }
    // Hover folgt jeden Frame der aktuellen Position (auch nach einem Overlay ohne Bewegung);
    // der Rasterfokus ändert sich dagegen nur bei echter Bewegung.
    const float x = in.pointerX, y = in.pointerY;
    const float viewBottom = grid_.viewTop() + grid_.viewHeight();
    hover_ = Hover::None;
    hoverBar_ = -1;
    if (contains(homeRect(ctx), x, y)) {
        hover_ = Hover::Home;
    } else if (contains(gearRect(ctx), x, y)) {
        hover_ = Hover::Gear;
    } else if (wifi_ != nullptr && contains(wifiRect(ctx), x, y)) {
        hover_ = Hover::Wifi;
    } else if (y >= viewBottom) {
        hover_ = Hover::InfoBar;
    } else if (y < grid_.viewTop()) {
        for (int i = 0; i < barCount_; ++i) {
            if (contains(barRect(i), x, y)) {
                hover_ = Hover::Bar;
                hoverBar_ = i;
                break;
            }
        }
    } else {
        // M23: Randstreifen oben/unten scrollen stetig, schneller zum Rand hin (höchstens eine Reihe je 0,25 s).
        const float top = grid_.viewTop(), bottom = viewBottom;
        float depth = 0.0f;
        if (y < top + kEdgeScrollPx) depth = -(top + kEdgeScrollPx - y) / kEdgeScrollPx;
        else if (y > bottom - kEdgeScrollPx) depth = (y - (bottom - kEdgeScrollPx)) / kEdgeScrollPx;
        bool scrolled = false;
        if (depth != 0.0f) {
            const float before = grid_.targetScroll();
            grid_.scrollBy(depth * grid_.rowPitch() / kEdgeRowMs * dtMs);
            scrolled = grid_.targetScroll() != before;
            if (scrolled) scroll_.snap(grid_.targetScroll());
        }
        if (in.pointerMoved || scrolled) {
            // Leere Fläche löscht den Fokus nur, wenn der Zeiger vorher eine Kachel fokussiert hatte;
            // ein Zeiger, der irgendwo neben dem Raster auftaucht, nimmt den Fokus nicht weg.
            const int hit = grid_.hitTest(x, y, scroll_.value());
            if (hit >= 0 || pointerOnTile_) grid_.setFocus(hit, false);
            pointerOnTile_ = hit >= 0;
        }
        return;
    }
    if (in.pointerMoved) {
        if (pointerOnTile_ && y >= viewBottom) grid_.setFocus(-1, false);
        pointerOnTile_ = false;
    }
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

    wifiAnimMs_ = ctx.wifi == WifiStatus::Connecting ? wifiAnimMs_ + dtMs : 0.0f;
    if (wifiAnimMs_ > 60000.0f) wifiAnimMs_ = 0.0f;

    const int focusBefore = grid_.focus(), barBefore = barIndex_;
    layoutBar(ctx);
    handlePointer(ctx, in, dtMs);

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
        } else if (hover_ == Hover::Wifi && wifi_ != nullptr) {
            out.push = wifi_;
        } else if (hover_ == Hover::Bar && hoverBar_ >= 0) {
            selectBar(hoverBar_, ctx.width, ctx.height);
            out.sound = UiSound::Confirm;
        } else if (hover_ == Hover::InfoBar) {
            // Klick auf freie Fläche der unteren Leiste öffnet kein Spiel.
        } else if (const Game* g = focusedGame(); g != nullptr && details_ != nullptr) {
            const bool fav = detailsStore_ != nullptr && detailsStore_->isFavorite(g->key);
            const u32 last = detailsStore_ != nullptr ? detailsStore_->lastPlayed(g->key) : 0;
            details_->open(*g, fav, last);
            if (playStats_ != nullptr) details_->setStats(playStats_->get(g->key));
            if ((g->flags & kFlagDisc) != 0) {
                char devLine[64];
                std::snprintf(devLine, sizeof devLine, tr(Str::DeviceLine), tr(Str::Disc));
                details_->setDeviceLine(devLine);
            } else if (g->console == Console::Channel) {
                char devLine[64];
                std::snprintf(devLine, sizeof devLine, tr(Str::DeviceLine), "EmuNAND");
                details_->setDeviceLine(devLine);
            } else if (detailsStore_ != nullptr) {
                details_->setDeviceLine(detailsDeviceLine(detailsStore_->devices(), g->device, detailsStore_->alsoOn(*g)));
            }
            syncDetailsCategories(g->key);
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

bool MainScene::isGameLocked(const Game& g) const {
    if (parental_ == nullptr) return false;
    return isLocked(*parental_, g.rating, overrides_ != nullptr && overrides_->locked(g.key));
}

void MainScene::drawTile(const UiContext& ctx, gfx::DrawList& dl, int index, Rect r, bool focused) const {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    if (focused) gfx::roundRect(dl, s, grow(r, 3), 13, t.accent);
    gfx::roundRect(dl, s, r, 10, focused ? t.tileFocus : t.tile);
    if (index < 0 || index >= static_cast<int>(games_.size())) return;
    const Game& g = *games_[index];

    CoverView cv{};
    float coverAlpha = 0.0f;
    if (covers_ != nullptr) {
        cv = covers_->view(g);
        if (cv.texture != 0) coverAlpha = cv.alpha;
    }

    // Platzhalter (Titel) blendet mit dem Cover aus (bei schmalen/hohen Covern sonst links/rechts dahinter sichtbar).
    const float textA = std::clamp(1.0f - coverAlpha, 0.0f, 1.0f);
    if (textA > 0.0f) {
        Color textColor = t.text;
        textColor.a = static_cast<u8>(static_cast<float>(textColor.a) * textA);
        const gfx::FontFace& body = ctx.fonts->face(FontSize::Small, FontWeight::Regular);
        const float maxW = r.w - 16.0f;
        gfx::Line lines[kTitleLines];
        const std::string_view title(g.title);
        const int n = gfx::wrapText(body, title, maxW, lines);
        float baseline = r.y + 34.0f + body.ascent;
        for (int i = 0; i < n; ++i) {
            const std::string_view line = title.substr(lines[i].start, lines[i].length);
            if (baseline + body.descent > r.y + r.h) break;
            gfx::drawTextEllipsis(dl, body, r.x + 8, baseline, maxW, line, textColor);
            baseline += body.lineHeight;
        }
    }

    if (cv.texture != 0 && cv.alpha > 0.0f) {
        const u8 a = static_cast<u8>(std::clamp(cv.alpha, 0.0f, 1.0f) * 255.0f);
        dl.quad(cv.texture, fitAspectRect(r, kCoverInset, cv.aspect), cv.uv, Color{255, 255, 255, a});
    }

    const gfx::FontFace& label = ctx.fonts->face(FontSize::Small, FontWeight::Medium);
    const bool disc = (g.flags & kFlagDisc) != 0;
    const char* tag = disc ? tr(Str::Disc) : consoleLabel(g.console);
    const float tagA = tagAlpha(disc, coverAlpha);
    auto faded = [tagA](Color c) {
        c.a = static_cast<u8>(static_cast<float>(c.a) * tagA);
        return c;
    };
    const float tagW = gfx::measureText(label, tag) + 12.0f;
    if (tagA > 0.0f) gfx::roundRect(dl, s, Rect{r.x + 8, r.y + 8, tagW, 18}, 9, faded(t.accentSoft));
    if (store_ != nullptr && store_->isFavorite(g.key)) gfx::circle(dl, s, r.x + r.w - 14, r.y + 17, 4, t.accent);
    if (isGameLocked(g)) {  // Schloss: Bügel als Ring, Körper als Rechteck, auf einer Plakette links vom Favoritenpunkt
        const float cx = r.x + r.w - 34, cy = r.y + 17;
        gfx::roundRect(dl, s, Rect{cx - 9, cy - 9, 18, 18}, 9, t.accentSoft);
        gfx::circle(dl, s, cx, cy - 2, 3.5f, t.accentSoftText);
        gfx::circle(dl, s, cx, cy - 2, 2.0f, t.accentSoft);
        gfx::fillRect(dl, s, Rect{cx - 4, cy - 1, 8, 6}, t.accentSoftText);
    }
    if (tagA > 0.0f)
        gfx::drawText(dl, label, r.x + 14, r.y + 8 + 9 + (label.ascent - label.descent) / 2, tag, faded(t.accentSoftText));
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
    const float barH = infoBarH(ctx), top = ctx.height - barH, mid = top + barH / 2;
    gfx::fillRect(dl, s, Rect{0, top, ctx.width, barH}, t.bar);
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
    gfx::drawText(dl, clock, clockX, mid + (clock.ascent - clock.descent) / 2, clockBuf_, t.text2);

    const Rect wifi = wifiRect(ctx);
    if (hover_ == Hover::Wifi) gfx::roundRect(dl, s, wifi, 10.0f, t.accentSoft);
    drawWifiIcon(dl, s, wifi.x + (wifi.w - 22.0f) / 2.0f, wifi.y + 22.0f, ctx.wifi, wifiAnimMs_, t);
    const gfx::FontFace& wifiFont = ctx.fonts->face(FontSize::Small, FontWeight::Medium);
    const char* wifiLabel = tr(Str::Wifi);
    const float wifiW = gfx::measureText(wifiFont, wifiLabel);
    const Color wifiColor = ctx.wifi == WifiStatus::Connected || ctx.wifi == WifiStatus::Limited ? t.accent
                          : ctx.wifi == WifiStatus::Connecting                                   ? t.text2
                                                                                                 : t.textMuted;
    gfx::drawText(dl, wifiFont, wifi.x + (wifi.w - wifiW) / 2.0f, wifi.y + 39.0f, wifiLabel, wifiColor);

    const float textX = home.x + home.w + 16.0f;
    const float maxW = wifi.x - 16.0f - textX;
    const gfx::FontFace& title = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    const gfx::FontFace& body = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    const char* line = infoLineText();
    if (line[0] == '\0') {
        gfx::drawTextEllipsis(dl, title, textX, mid + (title.ascent - title.descent) / 2, maxW, infoTitle(), t.text);
    } else {
        gfx::drawTextEllipsis(dl, title, textX, mid - 5, maxW, infoTitle(), t.text);
        gfx::drawTextEllipsis(dl, body, textX, mid + 17, maxW, line, t.text2);
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
    if (grid_.count() == 0) {
        const gfx::FontFace& f = ctx.fonts->face(FontSize::Title, FontWeight::Regular);
        const char* empty = searchActive_ && bar_[barIndex_].kind == BarKind::Search ? tr(Str::NoResults) : tr(Str::NoGames);
        const float w = gfx::measureText(f, empty);
        gfx::drawText(dl, f, (ctx.width - w) / 2, viewTop + grid_.viewHeight() / 2, empty, t.textMuted);
    }

    drawTopBar(ctx, dl);
    drawInfoBar(ctx, dl);
    // Die vergrößerte Fokus-Kachel ragt oben leicht in die Konsolenleiste, unten schneidet sie an der Taskbar ab.
    if (focus >= 0) {
        const Rect r = grid_.tileRect(focus, scroll);
        const float k = kFocusScale * focusAnim_.value();
        const Rect scaled{r.x - r.w * k / 2, r.y - r.h * k / 2, r.w * (1 + k), r.h * (1 + k)};
        const float pop = r.h * k / 2 + 3.0f;
        dl.setClip(Rect{0, viewTop - pop, ctx.width, viewBottom - viewTop + pop});
        drawTile(ctx, dl, focus, scaled, true);
        dl.clearClip();
    }
}

}  // namespace nl::ui
