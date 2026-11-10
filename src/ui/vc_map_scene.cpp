#include "ui/vc_map_scene.hpp"

#include <cstdio>

#include "gfx/text.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;

constexpr float kHeaderH = 64.0f, kFooterH = 56.0f, kMargin = 32.0f;
constexpr float kListTop = kHeaderH + 3.0f + 24.0f, kRowH = 40.0f, kRowGap = 4.0f;

std::string fill(const char* pattern, const std::string& arg) {
    char buf[160];
    std::snprintf(buf, sizeof buf, pattern, arg.c_str());
    return buf;
}
}  // namespace

void VcMapScene::open(std::vector<VcMapRow> rows, std::vector<ChannelInfo> channels) {
    rows_ = std::move(rows);
    channels_ = std::move(channels);
    right_.assign(rows_.size(), std::string());
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) refreshText(i);
    focus_ = 0;
    scroll_ = 0;
    dirty_ = false;
}

std::optional<u64> VcMapScene::selection(int row) const {
    if (row < 0 || row >= static_cast<int>(rows_.size())) return std::nullopt;
    return rows_[static_cast<std::size_t>(row)].mapped;
}

int VcMapScene::channelIndex(std::optional<u64> id) const {
    if (!id) return -1;
    for (std::size_t i = 0; i < channels_.size(); ++i)
        if (channels_[i].titleId == *id) return static_cast<int>(i);
    return -1;
}

void VcMapScene::select(int row, std::optional<u64> id) {
    VcMapRow& r = rows_[static_cast<std::size_t>(row)];
    if (r.mapped == id) return;
    r.mapped = id;
    dirty_ = true;
    refreshText(row);
}

void VcMapScene::refreshText(int row) {
    const VcMapRow& r = rows_[static_cast<std::size_t>(row)];
    std::string& out = right_[static_cast<std::size_t>(row)];
    const int idx = channelIndex(r.mapped);
    if (idx >= 0) out = channelDisplayName(channels_[static_cast<std::size_t>(idx)]);
    else if (r.mapped) out = fill(tr(Str::VcNotInstalled), formatTitleId(*r.mapped));
    else if (r.suggestion && *r.suggestion < channels_.size()) out = fill(tr(Str::VcSuggest), channelDisplayName(channels_[*r.suggestion]));
    else out = tr(Str::VcNone);
}

void VcMapScene::update(const UiContext&, const InputFrame& in, float, SceneRequests& out) {
    if (in.pressed(Action::Back)) {
        out.pop = true;
        if (dirty_) out.command = AppCommand::SaveVcMap;
        return;
    }
    const int n = static_cast<int>(rows_.size());
    if (n == 0) return;
    if (in.pressed(Action::Up) && focus_ > 0) --focus_;
    if (in.pressed(Action::Down) && focus_ + 1 < n) ++focus_;
    if (focus_ < scroll_) scroll_ = focus_;
    if (focus_ >= scroll_ + kVisibleRows) scroll_ = focus_ - kVisibleRows + 1;

    const int count = static_cast<int>(channels_.size());
    if (count == 0) return;
    const VcMapRow& r = rows_[static_cast<std::size_t>(focus_)];
    const int dir = in.pressed(Action::Right) ? 1 : in.pressed(Action::Left) ? -1 : 0;
    if (dir != 0) {
        // Positionen -1 (keiner) … count-1; eine fremde ID zählt wie „keiner“.
        const int next = (channelIndex(r.mapped) + 1 + dir + count + 1) % (count + 1) - 1;
        select(focus_, next < 0 ? std::nullopt : std::optional<u64>(channels_[static_cast<std::size_t>(next)].titleId));
        return;
    }
    if (in.pressed(Action::Confirm) && !r.mapped && r.suggestion && *r.suggestion < channels_.size())
        select(focus_, channels_[*r.suggestion].titleId);
}

void VcMapScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    gfx::fillRect(dl, s, Rect{0, 0, ctx.width, ctx.height}, t.bg);

    gfx::fillRect(dl, s, Rect{0, 0, ctx.width, kHeaderH}, t.bar);
    gfx::fillRect(dl, s, Rect{0, kHeaderH, ctx.width, 3}, t.accent);
    const gfx::FontFace& head = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    gfx::drawTextEllipsis(dl, head, kMargin, kHeaderH / 2 + (head.ascent - head.descent) / 2, ctx.width - 2 * kMargin,
                          tr(Str::VcMapTitle), t.text);

    const gfx::FontFace& body = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    const float w = ctx.width - 2 * kMargin;
    float y = kListTop;
    if (rows_.empty()) {
        gfx::drawTextEllipsis(dl, body, kMargin, y + body.ascent, w, tr(Str::VcNoGames), t.text2);
    } else {
        if (channels_.empty()) {
            gfx::drawTextEllipsis(dl, body, kMargin, y + body.ascent, w, tr(Str::VcNoChannels), t.text2);
            y += body.lineHeight + 8.0f;
        }
        const float leftW = w * 0.5f - 24.0f;
        const float rightX = kMargin + w * 0.5f;
        const float rightW = w * 0.5f - 16.0f;
        const int end = static_cast<int>(rows_.size()) < scroll_ + kVisibleRows ? static_cast<int>(rows_.size()) : scroll_ + kVisibleRows;
        for (int i = scroll_; i < end; ++i) {
            const Rect r{kMargin, y, w, kRowH};
            const bool focused = i == focus_;
            if (focused) gfx::roundRect(dl, s, Rect{r.x - 3, r.y - 3, r.w + 6, r.h + 6}, kRowH / 2 + 3, t.accent);
            gfx::roundRect(dl, s, r, kRowH / 2, focused ? t.tileFocus : t.tile);
            const float base = r.y + r.h / 2 + (body.ascent - body.descent) / 2;
            const VcMapRow& row = rows_[static_cast<std::size_t>(i)];
            gfx::drawTextEllipsis(dl, body, r.x + 16, base, leftW, row.title, t.text);
            gfx::drawTextEllipsis(dl, body, rightX, base, rightW, right_[static_cast<std::size_t>(i)],
                                  row.mapped ? t.text : t.text2);
            y += kRowH + kRowGap;
        }
    }

    const float top = ctx.height - kFooterH;
    gfx::fillRect(dl, s, Rect{0, top, ctx.width, kFooterH}, t.bar);
    gfx::fillRect(dl, s, Rect{0, top, ctx.width, 1}, t.barBorder);
    gfx::drawTextEllipsis(dl, body, kMargin, top + kFooterH / 2 + (body.ascent - body.descent) / 2, w, tr(Str::VcMapHelp), t.text2);
}

}  // namespace nl::ui
