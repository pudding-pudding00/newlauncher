#include "ui/emulators_scene.hpp"

#include <cstdio>

#include "gfx/text.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;

constexpr float kCardMaxW = 520.0f, kPad = 16.0f, kTitleH = 32.0f, kRowH = 30.0f, kHelpH = 24.0f, kInfoH = 24.0f;

bool contains(Rect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

Str errorStr(EmuInstallError e) {
    switch (e) {
        case EmuInstallError::NoSpace: return Str::EmuErrNoSpace;
        case EmuInstallError::Download: return Str::EmuErrDownload;
        case EmuInstallError::BadSize: return Str::EmuErrBadSize;
        case EmuInstallError::BadZip: return Str::EmuErrBadZip;
        case EmuInstallError::Write: return Str::EmuErrWrite;
        case EmuInstallError::Cancelled: return Str::EmuErrCancelled;
        case EmuInstallError::None: break;
    }
    return Str::EmuErrDownload;
}

Str stepStr(EmuStep s) {
    switch (s) {
        case EmuStep::Download: return Str::EmuStepDownload;
        case EmuStep::Verify: return Str::EmuStepVerify;
        case EmuStep::Extract: return Str::EmuStepExtract;
        case EmuStep::Move: return Str::EmuStepMove;
    }
    return Str::EmuStepDownload;
}

bool updatable(const EmuRow& r) {
    return r.remote.has_value() && (r.state == EmuState::UpdateAvailable || r.state == EmuState::UnknownVersion);
}
}  // namespace

void EmulatorsScene::onEnter() {
    focus_ = 0;
    keysSincePointerMove_ = false;
}

void EmulatorsScene::setError(std::string_view oscName, EmuInstallError e) {
    if (e == EmuInstallError::None)
        errors_.erase(std::string(oscName));
    else
        errors_[std::string(oscName)] = e;
}

const EmuRow* EmulatorsScene::selectedRow() const {
    return focus_ < static_cast<int>(rows_.size()) && focus_ < kEmuRows ? &rows_[static_cast<std::size_t>(focus_)]
                                                                        : nullptr;
}

std::vector<OscApp> EmulatorsScene::takeTargets() {
    std::vector<OscApp> out = std::move(targets_);
    targets_.clear();
    return out;
}

std::string EmulatorsScene::statusText(int row) const {
    if (row < 0 || row >= static_cast<int>(rows_.size())) return {};
    const EmuRow& r = rows_[static_cast<std::size_t>(row)];
    if (auto it = errors_.find(r.info->oscName); it != errors_.end()) return tr(errorStr(it->second));
    std::string s;
    switch (r.state) {
        case EmuState::NotInstalled: s = tr(Str::EmuNotInstalled); break;
        case EmuState::Current: s = tr(Str::EmuCurrent); break;
        case EmuState::UpdateAvailable: s = tr(Str::EmuUpdateAvailable); break;
        case EmuState::UnknownVersion: s = tr(Str::EmuUnknownVersion); break;
    }
    if (list_ == ListState::Loaded && !r.remote) return s + " · " + tr(Str::EmuNotInRepo);
    if (r.state == EmuState::UpdateAvailable && r.remote && !r.remote->version.empty()) s += " · " + r.remote->version;
    else if (r.state == EmuState::Current && r.installed && !r.installed->version.empty())
        s += " · " + r.installed->version;
    return s;
}

Rect EmulatorsScene::cardRect(const UiContext& ctx) const {
    const float w = ctx.width - 64.0f < kCardMaxW ? ctx.width - 64.0f : kCardMaxW;
    const float h = kPad + kTitleH + kRowCount * kRowH + kInfoH + kHelpH + kPad;
    return Rect{(ctx.width - w) / 2, (ctx.height - h) / 2, w, h};
}

Rect EmulatorsScene::rowRect(const UiContext& ctx, int row) const {
    const Rect c = cardRect(ctx);
    return Rect{c.x + kPad, c.y + kPad + kTitleH + static_cast<float>(row) * kRowH, c.w - 2 * kPad, kRowH};
}

int EmulatorsScene::hitTest(const UiContext& ctx, float x, float y) const {
    for (int i = 0; i < kRowCount; ++i)
        if (contains(rowRect(ctx, i), x, y)) return i;
    return -1;
}

void EmulatorsScene::update(const UiContext& ctx, const InputFrame& in, float, SceneRequests& out) {
    FocusSound sound(out, focus_);
    const int hit = in.pointerValid ? hitTest(ctx, in.pointerX, in.pointerY) : -1;
    if (in.pointerValid && in.pointerMoved) {
        if (hit >= 0) focus_ = hit;
        keysSincePointerMove_ = false;
    }
    if (in.pressed(Action::Up) && focus_ > 0) --focus_;
    if (in.pressed(Action::Down) && focus_ + 1 < kRowCount) ++focus_;
    if (in.pressed(Action::Up) || in.pressed(Action::Down)) keysSincePointerMove_ = true;
    const bool pointerOff = in.pointerValid && hit < 0 && !keysSincePointerMove_;

    if (in.pressed(Action::Back) || in.pressed(Action::Home)) {
        if (working())
            out.command = AppCommand::EmuCancel;
        else
            out.pop = true;
        return;
    }
    if (!in.pressed(Action::Confirm) || pointerOff || working()) return;

    if (focus_ == kEmuRows) {
        out.command = AppCommand::EmuFetchList;
        return;
    }
    if (list_ != ListState::Loaded) return;
    targets_.clear();
    if (focus_ == kEmuRows + 1) {
        for (const EmuRow& r : rows_)
            if (updatable(r)) targets_.push_back(*r.remote);
        if (!targets_.empty()) out.command = AppCommand::EmuInstallAll;
    } else if (const EmuRow* r = selectedRow(); r != nullptr && r->remote) {
        targets_.push_back(*r->remote);
        out.command = AppCommand::EmuInstall;
    }
    for (const OscApp& a : targets_) errors_.erase(a.name);
}

void EmulatorsScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    const Rect card = cardRect(ctx);
    gfx::fillRect(dl, s, Rect{0, 0, ctx.width, ctx.height}, t.overlayDim);
    gfx::roundRect(dl, s, card, 16.0f, t.bg);

    const gfx::FontFace& head = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    gfx::drawTextEllipsis(dl, head, card.x + kPad, card.y + kPad + head.ascent, card.w - 2 * kPad, tr(Str::Emulators),
                          t.text);

    const gfx::FontFace& label = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    const gfx::FontFace& small = ctx.fonts->face(FontSize::Small, FontWeight::Regular);
    for (int i = 0; i < kRowCount; ++i) {
        const Rect r = rowRect(ctx, i);
        if (i == focus_) gfx::roundRect(dl, s, Rect{r.x - 4, r.y + 1, r.w + 8, r.h - 2}, 8, t.tileFocus);
        const float base = r.y + r.h / 2 + (label.ascent - label.descent) / 2;
        if (i >= kEmuRows) {
            const Str text = i == kEmuRows ? Str::EmuCheckUpdates : Str::EmuUpdateAll;
            gfx::drawTextEllipsis(dl, label, r.x + 8, base, r.w - 16, tr(text), working() ? t.textMuted : t.accent);
            continue;
        }
        if (i >= static_cast<int>(rows_.size())) continue;
        const EmuRow& row = rows_[static_cast<std::size_t>(i)];
        const std::string status = statusText(i);
        const bool error = errors_.find(row.info->oscName) != errors_.end();
        const float sw = gfx::measureText(small, status);
        const float labelMax = r.w - 16 - sw - 12;
        gfx::drawTextEllipsis(dl, label, r.x + 8, base, labelMax > 0 ? labelMax : 0, row.info->label, t.text);
        gfx::drawText(dl, small, r.x + r.w - 8 - sw, base, status,
                      error ? t.danger : row.state == EmuState::UpdateAvailable ? t.accent : t.textMuted);
    }

    // Infozeile: Fortschritt, Listenzustand oder Hinweis.
    std::string info;
    if (busy_) {
        char buf[160];
        std::snprintf(buf, sizeof buf, tr(stepStr(busy_->step)), busy_->name.c_str());
        info = buf;
        if (busy_->count > 1) {
            std::snprintf(buf, sizeof buf, " (%u/%u)", static_cast<unsigned>(busy_->index + 1),
                          static_cast<unsigned>(busy_->count));
            info += buf;
        }
        if (busy_->total > 0) {
            const u64 pct = u64{busy_->done} * 100 / busy_->total;
            std::snprintf(buf, sizeof buf, "  %u %%", static_cast<unsigned>(pct > 100 ? 100 : pct));
            info += buf;
        }
    } else if (list_ == ListState::Loading) {
        info = tr(Str::EmuListLoading);
    } else if (list_ == ListState::Failed) {
        info = tr(Str::EmuListFailed);
    } else if (list_ == ListState::NotLoaded) {
        info = tr(Str::EmuListHint);
    }
    const float infoBase = rowRect(ctx, kRowCount).y + kInfoH / 2 + (small.ascent - small.descent) / 2;
    if (!info.empty())
        gfx::drawTextEllipsis(dl, small, card.x + kPad + 8, infoBase, card.w - 2 * kPad - 16, info,
                              list_ == ListState::Failed && !busy_ ? t.danger : t.text);

    const float helpBase = card.y + card.h - kPad - small.descent;
    gfx::drawTextEllipsis(dl, small, card.x + kPad, helpBase, card.w - 2 * kPad,
                          tr(working() ? Str::EmuBusyHelp : Str::EmuHelp), t.textMuted);
}

}  // namespace nl::ui
