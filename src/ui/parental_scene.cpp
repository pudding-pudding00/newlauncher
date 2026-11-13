#include "ui/parental_scene.hpp"

#include <string>

#include "gfx/text.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;

constexpr float kCardMaxW = 480.0f, kPad = 16.0f, kTitleH = 32.0f, kRowH = 26.0f, kHelpH = 24.0f;

bool contains(Rect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

Str labelFor(const ParentalConfig& c, ParentalRow r) {
    switch (r) {
        case ParentalRow::Pin: return c.enabled ? Str::ParentalChangePin : Str::ParentalSetPin;
        case ParentalRow::System: return Str::ParentalSystem;
        case ParentalRow::MaxAge: return Str::ParentalMaxAge;
        case ParentalRow::LockUnrated: return Str::ParentalLockUnrated;
        case ParentalRow::UseWii: return Str::ParentalUseWii;
        default: return Str::ParentalRemovePin;
    }
}

// "ab %u" / "%u+": %u von Hand ersetzen (kein Format aus einer Tabelle, -Wformat-nonliteral).
std::string ageText(u8 age) {
    std::string pattern = tr(Str::ParentalAgeYears);
    const std::size_t at = pattern.find("%u");
    if (at != std::string::npos) pattern.replace(at, 2, std::to_string(static_cast<unsigned>(age)));
    return pattern;
}
}  // namespace

std::string ParentalScene::valueTextFor(ParentalRow row) const {
    if (live_ == nullptr) return {};
    switch (row) {
        case ParentalRow::System: {
            std::string s = ratingSystemName(live_->system);
            for (char& ch : s) ch = static_cast<char>(ch - 'a' + 'A');
            return s;
        }
        case ParentalRow::MaxAge: return ageText(live_->maxAge);
        case ParentalRow::LockUnrated: return tr(live_->lockUnrated ? Str::On : Str::Off);
        case ParentalRow::UseWii: return tr(live_->useWii ? Str::On : Str::Off);
        case ParentalRow::Pin: return live_->enabled ? std::string() : std::string(tr(Str::ParentalOff));
        default: return {};
    }
}

void ParentalScene::onEnter() {
    focus_ = 0;
    if (live_ != nullptr) saved_ = *live_;
}

Rect ParentalScene::cardRect(const UiContext& ctx) const {
    const float w = ctx.width - 64.0f < kCardMaxW ? ctx.width - 64.0f : kCardMaxW;
    const float h = kPad + kTitleH + kCount * kRowH + kHelpH + kPad;
    return Rect{(ctx.width - w) / 2, (ctx.height - h) / 2, w, h};
}

Rect ParentalScene::rowRect(const UiContext& ctx, int row) const {
    const Rect c = cardRect(ctx);
    return Rect{c.x + kPad, c.y + kPad + kTitleH + static_cast<float>(row) * kRowH, c.w - 2 * kPad, kRowH};
}

void ParentalScene::update(const UiContext& ctx, const InputFrame& in, float, SceneRequests& out) {
    FocusSound sound(out, focus_);
    if (in.pressed(Action::Back) || in.pressed(Action::Home) || live_ == nullptr) {
        if (live_ == nullptr && !in.pressed(Action::Back) && !in.pressed(Action::Home)) return;
        out.pop = true;
        if (live_ != nullptr && !(*live_ == saved_)) {
            out.command = AppCommand::SaveSettings;
            saved_ = *live_;
        }
        return;
    }
    int hit = -1;
    if (in.pointerValid) {
        for (int i = 0; i < kCount; ++i)
            if (contains(rowRect(ctx, i), in.pointerX, in.pointerY)) hit = i;
        if (in.pointerMoved && hit >= 0) focus_ = hit;
    }
    if (in.pressed(Action::Up) && focus_ > 0) --focus_;
    if (in.pressed(Action::Down) && focus_ + 1 < kCount) ++focus_;

    const ParentalRow row = static_cast<ParentalRow>(focus_);
    const bool pointerOff = in.pointerValid && hit < 0;
    const bool confirm = in.pressed(Action::Confirm) && !pointerOff;
    if (!parentalRowEnabled(*live_, row)) return;
    if (isParentalAction(row)) {
        if (confirm) out.command = row == ParentalRow::Pin ? AppCommand::ChangePin : AppCommand::RemovePin;
        return;
    }
    int dir = 0;
    if (in.pressed(Action::Right) || confirm) dir = 1;
    else if (in.pressed(Action::Left)) dir = -1;
    if (dir != 0) {
        stepParental(*live_, row, dir);
        out.command = AppCommand::SettingsChanged;
    }
}

void ParentalScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    const Rect card = cardRect(ctx);
    gfx::fillRect(dl, s, Rect{0, 0, ctx.width, ctx.height}, t.overlayDim);
    gfx::roundRect(dl, s, card, 16.0f, t.bg);

    const gfx::FontFace& head = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    gfx::drawTextEllipsis(dl, head, card.x + kPad, card.y + kPad + head.ascent, card.w - 2 * kPad, tr(Str::Parental), t.text);

    const gfx::FontFace& label = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    const gfx::FontFace& value = ctx.fonts->face(FontSize::Body, FontWeight::Medium);
    const ParentalConfig shown = live_ != nullptr ? *live_ : ParentalConfig{};
    for (int i = 0; i < kCount; ++i) {
        const Rect r = rowRect(ctx, i);
        const ParentalRow row = static_cast<ParentalRow>(i);
        const bool enabled = parentalRowEnabled(shown, row);
        if (i == focus_) gfx::roundRect(dl, s, Rect{r.x - 4, r.y + 1, r.w + 8, r.h - 2}, 8, t.tileFocus);
        const float base = r.y + r.h / 2 + (label.ascent - label.descent) / 2;
        const Color fg = enabled ? t.text : t.textMuted;
        const std::string v = valueTextFor(row);
        const float vw = v.empty() ? 0.0f : gfx::measureText(value, v);
        const float labelMax = r.w - 16 - vw - (vw > 0 ? 12 : 0);
        gfx::drawTextEllipsis(dl, label, r.x + 8, base, labelMax > 0 ? labelMax : 0, tr(labelFor(shown, row)),
                              isParentalAction(row) && enabled ? t.accent : fg);
        if (!v.empty()) gfx::drawText(dl, value, r.x + r.w - 8 - vw, base, v, fg);
    }
    const gfx::FontFace& small = ctx.fonts->face(FontSize::Small, FontWeight::Regular);
    gfx::drawTextEllipsis(dl, small, card.x + kPad, card.y + card.h - kPad - small.descent, card.w - 2 * kPad,
                          tr(Str::SettingsHelp), t.textMuted);
}

}  // namespace nl::ui
