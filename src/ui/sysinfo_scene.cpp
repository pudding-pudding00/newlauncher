#include "ui/sysinfo_scene.hpp"

#include <cstdio>
#include <utility>

#include "gfx/text.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;

constexpr float kCardMaxW = 520.0f, kCardMaxH = 432.0f, kPad = 16.0f, kTitleH = 32.0f, kLineH = 22.0f,
                kHelpH = 24.0f;
}  // namespace

void SysInfoScene::setInfo(SystemInfo info) {
    info_ = std::move(info);
    lines_ = sysInfoLines(info_, language() == Lang::De);
}

void SysInfoScene::onEnter() {
    scroll_ = 0;
    status_.clear();
    lines_ = sysInfoLines(info_, language() == Lang::De);
}

void SysInfoScene::setSaveResult(std::string_view path, bool ok) {
    if (!ok) {
        status_ = tr(Str::SysInfoSaveFailed);
        return;
    }
    char buf[160];
    std::snprintf(buf, sizeof buf, tr(Str::SysInfoSaved), std::string(path).c_str());
    status_ = buf;
}

Rect SysInfoScene::cardRect(const UiContext& ctx) const {
    const float w = ctx.width - 64.0f < kCardMaxW ? ctx.width - 64.0f : kCardMaxW;
    const float h = ctx.height - 48.0f < kCardMaxH ? ctx.height - 48.0f : kCardMaxH;
    return Rect{(ctx.width - w) / 2, (ctx.height - h) / 2, w, h};
}

int SysInfoScene::visibleLines(const UiContext& ctx) const {
    const float avail = cardRect(ctx).h - 2 * kPad - kTitleH - kHelpH;
    const int n = static_cast<int>(avail / kLineH);
    return n < 1 ? 1 : n;
}

int SysInfoScene::maxScroll(const UiContext& ctx) const {
    const int m = static_cast<int>(lines_.size()) - visibleLines(ctx);
    return m < 0 ? 0 : m;
}

void SysInfoScene::update(const UiContext& ctx, const InputFrame& in, float, SceneRequests& out) {
    FocusSound sound(out, scroll_);
    if (in.pressed(Action::Back) || in.pressed(Action::Home)) {
        out.pop = true;
        return;
    }
    if (in.pressed(Action::Confirm)) {
        out.command = AppCommand::SaveSysInfo;
        return;
    }
    if (in.pressed(Action::Up) && scroll_ > 0) --scroll_;
    if (in.pressed(Action::Down) && scroll_ < maxScroll(ctx)) ++scroll_;
}

void SysInfoScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    const Rect card = cardRect(ctx);
    gfx::fillRect(dl, s, Rect{0, 0, ctx.width, ctx.height}, t.overlayDim);
    gfx::roundRect(dl, s, card, 16.0f, t.bg);

    const gfx::FontFace& head = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    gfx::drawTextEllipsis(dl, head, card.x + kPad, card.y + kPad + head.ascent, card.w - 2 * kPad,
                          tr(Str::SysInfo), t.text);

    const gfx::FontFace& body = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    const gfx::FontFace& small = ctx.fonts->face(FontSize::Small, FontWeight::Regular);
    const int first = scroll_;
    const int last = first + visibleLines(ctx);
    const float top = card.y + kPad + kTitleH;
    for (int i = first; i < last && i < static_cast<int>(lines_.size()); ++i) {
        const float y = top + static_cast<float>(i - first) * kLineH;
        const std::string& l = lines_[static_cast<std::size_t>(i)];
        const bool indented = l.size() > 1 && l[0] == ' ';
        const gfx::FontFace& f = indented ? small : body;
        const float base = y + kLineH / 2 + (f.ascent - f.descent) / 2;
        gfx::drawTextEllipsis(dl, f, card.x + kPad, base, card.w - 2 * kPad, l, indented ? t.textMuted : t.text);
    }

    const float helpBase = card.y + card.h - kPad - small.descent;
    const std::string& help = status_.empty() ? std::string(tr(Str::SysInfoHelp)) : status_;
    gfx::drawTextEllipsis(dl, small, card.x + kPad, helpBase, card.w - 2 * kPad, help, t.textMuted);
}

}  // namespace nl::ui
