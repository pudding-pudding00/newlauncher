#include "ui/saves_scene.hpp"

#include <cstdio>
#include <utility>

#include "gfx/text.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;

constexpr float kCardMaxW = 560.0f, kCardMaxH = 400.0f, kPad = 24.0f, kRowH = 26.0f, kButtonH = 44.0f,
                kButtonGap = 12.0f;

bool contains(Rect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

constexpr Str kLabels[SavesScene::kButtonCount] = {Str::SavesBackup, Str::SavesRestore, Str::SavesDelete};
}  // namespace

std::string formatBytes(u64 bytes) {
    char buf[32];
    if (bytes < 1024) {
        std::snprintf(buf, sizeof buf, "%u B", static_cast<unsigned>(bytes));
    } else if (bytes < 1024ull * 1024ull) {
        std::snprintf(buf, sizeof buf, "%u KB", static_cast<unsigned>((bytes + 512) / 1024));
    } else {
        const unsigned tenths = static_cast<unsigned>((bytes * 10 + 512 * 1024) / (1024ull * 1024ull));
        std::snprintf(buf, sizeof buf, "%u%c%u MB", tenths / 10, language() == Lang::De ? ',' : '.', tenths % 10);
    }
    return buf;
}

std::string backupLabel(std::string_view n) {
    if (n.size() != 19 || n[8] != '-' || n.substr(15) != ".zip") return std::string(n);
    for (std::size_t i = 0; i < 15; ++i)
        if (i != 8 && (n[i] < '0' || n[i] > '9')) return std::string(n);
    const std::string_view y = n.substr(0, 4), mo = n.substr(4, 2), d = n.substr(6, 2);
    std::string time = std::string(n.substr(9, 2)) + ":" + std::string(n.substr(11, 2)) + ":" + std::string(n.substr(13, 2));
    if (language() == Lang::De) return std::string(d) + "." + std::string(mo) + "." + std::string(y) + " " + time;
    return std::string(y) + "-" + std::string(mo) + "-" + std::string(d) + " " + time;
}

void SavesScene::open(const Game& g, SavesView v) {
    key_ = g.key != nullptr ? g.key : "";
    title_ = g.title != nullptr ? g.title : "";
    game_ = g;
    game_.key = key_.c_str();
    game_.title = title_.c_str();
    game_.description = nullptr;
    game_.path = nullptr;
    hasGame_ = true;
    status_.clear();
    view_ = std::move(v);
    selected_ = view_.backups.empty() ? -1 : 0;
    scroll_ = 0;
    focus_ = -1;
    clampSelection();
}

void SavesScene::setView(SavesView v) {
    view_ = std::move(v);
    clampSelection();
}

bool SavesScene::hasButtons() const {
    return view_.state == SavesView::State::Ok || view_.state == SavesView::State::Empty;
}

bool SavesScene::buttonUsable(int b) const {
    if (!hasButtons()) return false;
    switch (b) {
        case kButtonBackup: return view_.state == SavesView::State::Ok;
        case kButtonRestore:
        case kButtonDelete: return !view_.backups.empty();
        default: return false;
    }
}

void SavesScene::clampSelection() {
    const int n = static_cast<int>(view_.backups.size());
    if (n == 0) selected_ = -1;
    else if (selected_ < 0) selected_ = 0;
    else if (selected_ >= n) selected_ = n - 1;
    if (scroll_ > selected_) scroll_ = selected_ < 0 ? 0 : selected_;
    if (selected_ >= scroll_ + kVisibleRows) scroll_ = selected_ - kVisibleRows + 1;
    if (focus_ >= 0 && buttonUsable(focus_)) return;
    focus_ = -1;
    for (int b = 0; b < kButtonCount && focus_ < 0; ++b)
        if (buttonUsable(b)) focus_ = b;
}

const char* SavesScene::stateText() const {
    switch (view_.state) {
        case SavesView::State::NandLocked: return tr(Str::SavesNandLocked);
        case SavesView::State::Unknown: return tr(Str::SavesUnknown);
        case SavesView::State::Empty: return tr(Str::SavesEmpty);
        case SavesView::State::Ok: break;
    }
    std::snprintf(stateText_, sizeof stateText_, tr(Str::SavesSize), formatBytes(view_.bytes).c_str(),
                  static_cast<unsigned>(view_.files));
    return stateText_;
}

Rect SavesScene::cardRect(const UiContext& ctx) const {
    const float w = ctx.width - 64.0f < kCardMaxW ? ctx.width - 64.0f : kCardMaxW;
    const float h = ctx.height - 48.0f < kCardMaxH ? ctx.height - 48.0f : kCardMaxH;
    return Rect{(ctx.width - w) / 2, (ctx.height - h) / 2, w, h};
}

Rect SavesScene::buttonRect(const UiContext& ctx, int b) const {
    if (b < 0 || b >= kButtonCount) return Rect{0, 0, 0, 0};
    const Rect c = cardRect(ctx);
    const float inner = c.w - 2 * kPad;
    const float bw = (inner - (kButtonCount - 1) * kButtonGap) / kButtonCount;
    return Rect{c.x + kPad + static_cast<float>(b) * (bw + kButtonGap), c.y + c.h - kPad - kButtonH, bw, kButtonH};
}

void SavesScene::update(const UiContext& ctx, const InputFrame& in, float, SceneRequests& out) {
    if (in.pressed(Action::Back)) {
        out.pop = true;
        out.sound = UiSound::Back;
        return;
    }
    const int n = static_cast<int>(view_.backups.size());
    if (in.pressed(Action::Down) && selected_ >= 0 && selected_ + 1 < n) {
        ++selected_;
        out.sound = UiSound::Move;
    }
    if (in.pressed(Action::Up) && selected_ > 0) {
        --selected_;
        out.sound = UiSound::Move;
    }
    clampSelection();
    const int dir = in.pressed(Action::Right) ? 1 : in.pressed(Action::Left) ? -1 : 0;
    if (dir != 0 && focus_ >= 0) {
        for (int b = focus_ + dir; b >= 0 && b < kButtonCount; b += dir) {
            if (!buttonUsable(b)) continue;
            focus_ = b;
            out.sound = UiSound::Move;
            break;
        }
    }
    int hit = -1;
    if (in.pointerValid) {
        for (int b = 0; b < kButtonCount; ++b)
            if (buttonUsable(b) && contains(buttonRect(ctx, b), in.pointerX, in.pointerY)) hit = b;
        if (in.pointerMoved && hit >= 0) focus_ = hit;
    }
    if (!in.pressed(Action::Confirm) || focus_ < 0 || !buttonEnabled(focus_)) return;
    if (in.pointerValid && hit < 0) return;  // mit sichtbarem Zeiger zählt A nur auf einem Knopf
    static constexpr AppCommand kCommands[kButtonCount] = {AppCommand::BackupSave, AppCommand::RestoreSave,
                                                           AppCommand::DeleteBackup};
    out.command = kCommands[focus_];
    out.game = &game_;
    out.sound = UiSound::Confirm;
}

void SavesScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    gfx::fillRect(dl, s, Rect{0, 0, ctx.width, ctx.height}, t.overlayDim);
    const Rect c = cardRect(ctx);
    gfx::roundRect(dl, s, c, 16.0f, t.bg);
    const gfx::FontFace& head = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    const gfx::FontFace& body = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    const float x = c.x + kPad, w = c.w - 2 * kPad;
    float y = c.y + kPad;

    char title[160];
    std::snprintf(title, sizeof title, tr(Str::SavesTitle), title_.c_str());
    gfx::drawTextEllipsis(dl, head, x, y + head.ascent, w, title, t.text);
    y += head.lineHeight + 4.0f;
    if (!view_.location.empty()) {
        gfx::drawTextEllipsis(dl, body, x, y + body.ascent, w, view_.location, t.textMuted);
        y += body.lineHeight;
    }
    gfx::drawTextEllipsis(dl, body, x, y + body.ascent, w, stateText(), t.text2);
    y += body.lineHeight + 8.0f;

    // Sicherungen: höchstens kVisibleRows, die gewählte hervorgehoben
    if (view_.backups.empty()) {
        gfx::drawTextEllipsis(dl, body, x, y + body.ascent, w, tr(Str::SavesNoBackup), t.textMuted);
    } else {
        const int n = static_cast<int>(view_.backups.size());
        for (int i = scroll_; i < n && i < scroll_ + kVisibleRows; ++i) {
            const Rect row{x, y, w, kRowH};
            const bool sel = i == selected_;
            if (sel) gfx::roundRect(dl, s, row, kRowH / 2, t.accentSoft);
            gfx::drawTextEllipsis(dl, body, x + 12.0f, y + kRowH / 2 + (body.ascent - body.descent) / 2, w - 24.0f,
                                  view_.backups[static_cast<std::size_t>(i)], sel ? t.accentSoftText : t.text);
            y += kRowH;
        }
    }

    const float buttonsTop = buttonRect(ctx, 0).y;
    if (!status_.empty())
        gfx::drawTextEllipsis(dl, body, x, buttonsTop - 8.0f - body.descent, w, status_, t.text2);
    if (!hasButtons()) return;
    const gfx::FontFace& label = ctx.fonts->face(FontSize::Body, FontWeight::Medium);
    for (int b = 0; b < kButtonCount; ++b) {
        const Rect r = buttonRect(ctx, b);
        const bool enabled = buttonEnabled(b);
        const bool focus = enabled && b == focus_;
        gfx::roundRect(dl, s, r, kButtonH / 2, focus ? t.accent : enabled ? t.accentSoft : t.tile);
        const char* text = tr(kLabels[b]);
        const float inner = r.w - 16.0f;
        const float tw = gfx::measureText(label, text);
        gfx::drawTextEllipsis(dl, label, tw < inner ? r.x + (r.w - tw) / 2 : r.x + 8.0f,
                              r.y + r.h / 2 + (label.ascent - label.descent) / 2, inner, text,
                              focus ? t.onAccent : enabled ? t.accentSoftText : t.textMuted);
    }
}

}  // namespace nl::ui
