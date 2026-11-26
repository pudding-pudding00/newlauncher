#include "ui/wifi_scene.hpp"

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

std::string fmtText(Str s, const char* arg) {
    char buf[160];
    std::snprintf(buf, sizeof buf, tr(s), arg);
    return buf;
}
std::string fmtInt(Str s, int v) {
    char buf[160];
    std::snprintf(buf, sizeof buf, tr(s), v);
    return buf;
}
std::string fmtUnsigned(Str s, unsigned v) {
    char buf[160];
    std::snprintf(buf, sizeof buf, tr(s), v);
    return buf;
}
}  // namespace

const char* wifiStatusText(WifiStatus s) {
    switch (s) {
        case WifiStatus::Connected: return tr(Str::WifiConnected);
        case WifiStatus::Limited: return tr(Str::WifiLimited);
        case WifiStatus::Connecting: return tr(Str::WifiConnecting);
        case WifiStatus::Disconnected: break;
    }
    return tr(Str::WifiDisconnected);
}

std::vector<std::string> wifiInfoLines(const WifiSnapshot& s, u64 nowMs) {
    std::vector<std::string> out;
    char buf[32];
    out.push_back(fmtText(Str::WifiLineState, wifiStatusText(s.status)));
    const bool up = s.status == WifiStatus::Connected || s.status == WifiStatus::Limited;
    if (up || s.ip != 0) {
        formatIpv4(s.ip, buf, sizeof buf);
        out.push_back(fmtText(Str::WifiLineIp, buf));
        formatIpv4(s.netmask, buf, sizeof buf);
        out.push_back(fmtText(Str::WifiLineMask, buf));
        formatIpv4(s.gateway, buf, sizeof buf);
        out.push_back(fmtText(Str::WifiLineGateway, buf));
        out.push_back(s.gatewayMs >= 0 ? fmtInt(Str::WifiLineRouterMs, static_cast<int>(s.gatewayMs))
                                       : std::string(tr(Str::WifiLineRouterNone)));
    }
    if (s.macKnown) {
        formatMac(s.mac, buf, sizeof buf);
        out.push_back(fmtText(Str::WifiLineMac, buf));
    }
    if (up && s.connectedSinceMs != 0 && nowMs >= s.connectedSinceMs) {
        formatDuration(nowMs - s.connectedSinceMs, buf, sizeof buf);
        out.push_back(fmtText(Str::WifiLineSince, buf));
    }
    if (s.lastInitCode < 0) out.push_back(fmtInt(Str::WifiLineCode, static_cast<int>(s.lastInitCode)));
    if (s.failedProbes > 0) out.push_back(fmtUnsigned(Str::WifiLineFails, static_cast<unsigned>(s.failedProbes)));
    if (s.reconnects > 0) out.push_back(fmtUnsigned(Str::WifiLineReconnects, static_cast<unsigned>(s.reconnects)));
    if (!s.checked)
        out.push_back(tr(Str::WifiLineNotChecked));
    else if (nowMs >= s.lastCheckMs)
        out.push_back(fmtUnsigned(Str::WifiLineChecked, static_cast<unsigned>((nowMs - s.lastCheckMs) / 1000)));
    if (s.tlsBuild) {
        char line[64];
        std::snprintf(line, sizeof line, tr(Str::AboutHttps), tr(s.tlsUp ? Str::On : Str::Off));
        out.push_back(line);
    }
    if (s.status == WifiStatus::Disconnected || s.status == WifiStatus::Limited) {
        out.push_back(tr(Str::WifiTips));
        for (Str t : {Str::WifiTip1, Str::WifiTip2, Str::WifiTip3, Str::WifiTip4, Str::WifiTip5, Str::WifiTip6})
            out.push_back(tr(t));
    }
    return out;
}

void WifiScene::setSnapshot(const WifiSnapshot& s, u64 nowMs) {
    snap_ = s;
    lines_ = wifiInfoLines(s, nowMs);
}

Rect WifiScene::cardRect(const UiContext& ctx) const {
    const float w = ctx.width - 64.0f < kCardMaxW ? ctx.width - 64.0f : kCardMaxW;
    const float h = ctx.height - 48.0f < kCardMaxH ? ctx.height - 48.0f : kCardMaxH;
    return Rect{(ctx.width - w) / 2, (ctx.height - h) / 2, w, h};
}

int WifiScene::visibleLines(const UiContext& ctx) const {
    const float avail = cardRect(ctx).h - 2 * kPad - kTitleH - kHelpH;
    const int n = static_cast<int>(avail / kLineH);
    return n < 1 ? 1 : n;
}

int WifiScene::maxScroll(const UiContext& ctx) const {
    const int m = static_cast<int>(lines_.size()) - visibleLines(ctx);
    return m < 0 ? 0 : m;
}

void WifiScene::update(const UiContext& ctx, const InputFrame& in, float, SceneRequests& out) {
    FocusSound sound(out, scroll_);
    if (scroll_ > maxScroll(ctx)) scroll_ = maxScroll(ctx);
    if (in.pressed(Action::Back) || in.pressed(Action::Home)) {
        out.pop = true;
        return;
    }
    if (in.pressed(Action::Confirm) && snap_.status != WifiStatus::Connecting) {
        out.command = AppCommand::WifiReconnect;
        return;
    }
    if (in.pressed(Action::Up) && scroll_ > 0) --scroll_;
    if (in.pressed(Action::Down) && scroll_ < maxScroll(ctx)) ++scroll_;
}

void WifiScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    const Rect card = cardRect(ctx);
    gfx::fillRect(dl, s, Rect{0, 0, ctx.width, ctx.height}, t.overlayDim);
    gfx::roundRect(dl, s, card, 16.0f, t.bg);

    const gfx::FontFace& head = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    gfx::drawTextEllipsis(dl, head, card.x + kPad, card.y + kPad + head.ascent, card.w - 2 * kPad, tr(Str::Wifi),
                          t.text);

    const gfx::FontFace& body = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    const gfx::FontFace& small = ctx.fonts->face(FontSize::Small, FontWeight::Regular);
    const int first = scroll_;
    const int last = first + visibleLines(ctx);
    const float top = card.y + kPad + kTitleH;
    for (int i = first; i < last && i < static_cast<int>(lines_.size()); ++i) {
        const float y = top + static_cast<float>(i - first) * kLineH;
        const std::string& l = lines_[static_cast<std::size_t>(i)];
        const bool tip = l.size() > 1 && l[0] == ' ';
        const gfx::FontFace& f = tip ? small : body;
        const float base = y + kLineH / 2 + (f.ascent - f.descent) / 2;
        Color c = tip ? t.textMuted : t.text;
        if (i == 0 && snap_.status == WifiStatus::Connected) c = t.accent;
        else if (i == 0 && snap_.status == WifiStatus::Disconnected) c = t.danger;
        gfx::drawTextEllipsis(dl, f, card.x + kPad, base, card.w - 2 * kPad, l, c);
    }

    const float helpBase = card.y + card.h - kPad - small.descent;
    const char* help = tr(snap_.status == WifiStatus::Connecting ? Str::WifiBusyHelp : Str::WifiHelp);
    gfx::drawTextEllipsis(dl, small, card.x + kPad, helpBase, card.w - 2 * kPad, help, t.textMuted);
}

}  // namespace nl::ui
