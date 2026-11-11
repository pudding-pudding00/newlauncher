#include "ui/about_scene.hpp"

#include <cstdio>

#include "gfx/text.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;

constexpr float kCardMaxW = 520.0f, kCardMaxH = 432.0f, kPad = 16.0f, kTitleH = 32.0f, kLineH = 22.0f,
                kHelpH = 24.0f;

constexpr AboutEntry kEntries[] = {
#include "about_entries.inc"
};
}  // namespace

std::span<const AboutEntry> aboutEntries() { return kEntries; }

void AboutScene::setBuildInfo(std::string_view version, bool https) {
    version_ = std::string(version);
    https_ = https;
    rebuild();
}

void AboutScene::onEnter() {
    scroll_ = 0;
    rebuild();
}

void AboutScene::rebuild() {
    lines_.clear();
    lines_.push_back("newlauncher " + version_);
    char buf[64];
    std::snprintf(buf, sizeof buf, tr(Str::AboutHttps), tr(https_ ? Str::On : Str::Off));
    lines_.emplace_back(buf);
    lines_.emplace_back(tr(Str::AboutGpl));
    lines_.emplace_back(tr(Str::AboutSource));
    headingLine_ = static_cast<int>(lines_.size());
    lines_.emplace_back(tr(Str::AboutComponents));
    for (const AboutEntry& e : kEntries) {
        std::string l = e.name;
        l += " ";
        l += e.version;
        l += " · ";
        l += e.license;
        lines_.push_back(std::move(l));
    }
}

Rect AboutScene::cardRect(const UiContext& ctx) const {
    const float w = ctx.width - 64.0f < kCardMaxW ? ctx.width - 64.0f : kCardMaxW;
    const float h = ctx.height - 48.0f < kCardMaxH ? ctx.height - 48.0f : kCardMaxH;
    return Rect{(ctx.width - w) / 2, (ctx.height - h) / 2, w, h};
}

int AboutScene::visibleLines(const UiContext& ctx) const {
    const float avail = cardRect(ctx).h - 2 * kPad - kTitleH - kHelpH;
    const int n = static_cast<int>(avail / kLineH);
    return n < 1 ? 1 : n;
}

int AboutScene::maxScroll(const UiContext& ctx) const {
    const int m = static_cast<int>(lines_.size()) - visibleLines(ctx);
    return m < 0 ? 0 : m;
}

void AboutScene::update(const UiContext& ctx, const InputFrame& in, float, SceneRequests& out) {
    FocusSound sound(out, scroll_);
    if (in.pressed(Action::Back) || in.pressed(Action::Home)) {
        out.pop = true;
        return;
    }
    if (in.pressed(Action::Up) && scroll_ > 0) --scroll_;
    if (in.pressed(Action::Down) && scroll_ < maxScroll(ctx)) ++scroll_;
}

void AboutScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    const Rect card = cardRect(ctx);
    gfx::fillRect(dl, s, Rect{0, 0, ctx.width, ctx.height}, t.overlayDim);
    gfx::roundRect(dl, s, card, 16.0f, t.bg);

    const gfx::FontFace& head = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    gfx::drawTextEllipsis(dl, head, card.x + kPad, card.y + kPad + head.ascent, card.w - 2 * kPad, tr(Str::About),
                          t.text);

    const gfx::FontFace& body = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    const gfx::FontFace& bold = ctx.fonts->face(FontSize::Body, FontWeight::Medium);
    const gfx::FontFace& small = ctx.fonts->face(FontSize::Small, FontWeight::Regular);
    const int first = scroll_;
    const int last = first + visibleLines(ctx);
    const float top = card.y + kPad + kTitleH;
    for (int i = first; i < last && i < static_cast<int>(lines_.size()); ++i) {
        const float y = top + static_cast<float>(i - first) * kLineH;
        const bool heading = i == headingLine_ || i == 0;
        const bool entry = i > headingLine_;
        const gfx::FontFace& f = heading ? bold : entry ? small : body;
        const float base = y + kLineH / 2 + (f.ascent - f.descent) / 2;
        gfx::drawTextEllipsis(dl, f, card.x + kPad, base, card.w - 2 * kPad, lines_[static_cast<std::size_t>(i)],
                              entry ? t.textMuted : t.text);
    }

    const float helpBase = card.y + card.h - kPad - small.descent;
    gfx::drawTextEllipsis(dl, small, card.x + kPad, helpBase, card.w - 2 * kPad, tr(Str::AboutHelp), t.textMuted);
}

}  // namespace nl::ui
