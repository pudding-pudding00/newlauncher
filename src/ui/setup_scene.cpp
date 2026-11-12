#include "ui/setup_scene.hpp"

#include <array>
#include <cstdio>
#include <string>
#include <utility>

#include "gfx/text.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;

constexpr float kCardMaxW = 520.0f, kCardMaxH = 432.0f, kPad = 16.0f, kTitleH = 32.0f, kHeadH = 28.0f,
                kTextLineH = 20.0f, kOptionH = 30.0f, kHelpH = 24.0f;
constexpr int kTextLines = 3, kMaxOptions = 7;

struct StepText {
    Str title, text;
};

StepText stepText(WizardStep s) {
    switch (s) {
        case WizardStep::Language: return {Str::SetupLanguage, Str::SetupLanguageText};
        case WizardStep::Sources: return {Str::SetupSources, Str::SetupSourcesText};
        case WizardStep::Cios: return {Str::SetupCios, Str::SetupCiosText};
        case WizardStep::Emulators: return {Str::SetupEmulators, Str::SetupEmulatorsText};
        case WizardStep::Network: return {Str::SetupNetwork, Str::SetupNetworkText};
        default: return {Str::SetupDone, Str::SetupDoneText};
    }
}

Str netText(SetupScene::NetTest n) {
    switch (n) {
        case SetupScene::NetTest::Ok: return Str::SetupNetOk;
        case SetupScene::NetTest::OkTimeSet: return Str::SetupNetOkTime;
        case SetupScene::NetTest::Fail: return Str::SetupNetFail;
        default: return Str::SetupNetRunning;
    }
}
}  // namespace

void SetupScene::start(SetupWizard wizard) {
    wizard_.emplace(std::move(wizard));
    net_ = NetTest::Idle;
    netRequested_ = false;
}

Rect SetupScene::cardRect(const UiContext& ctx) const {
    const float w = ctx.width - 64.0f < kCardMaxW ? ctx.width - 64.0f : kCardMaxW;
    const float h = ctx.height - 48.0f < kCardMaxH ? ctx.height - 48.0f : kCardMaxH;
    return Rect{(ctx.width - w) / 2, (ctx.height - h) / 2, w, h};
}

void SetupScene::update(const UiContext&, const InputFrame& in, float, SceneRequests& out) {
    if (!wizard_) {
        out.pop = true;
        return;
    }
    SetupWizard& w = *wizard_;
    const WizardStep stepBefore = w.step();
    int focus = static_cast<int>(w.selected());
    FocusSound sound(out, focus);
    if (in.pressed(Action::Home)) {
        out.pop = true;
        return;
    }
    if (in.pressed(Action::Up) || in.pressed(Action::Left)) w.select(-1);
    else if (in.pressed(Action::Down) || in.pressed(Action::Right)) w.select(1);
    else if (in.pressed(Action::PrevConsole)) w.back();
    else if (in.pressed(Action::Back)) {
        if (w.step() == WizardStep::Done) w.back();
        else w.skip();
    } else if (in.pressed(Action::Confirm)) {
        if (w.step() == WizardStep::Done) {
            out.command = AppCommand::FinishSetup;
            out.pop = true;
            return;
        }
        if (w.installSelected()) {
            out.command = AppCommand::OpenEmulators;
            return;
        }
        w.accept();
    }
    focus = static_cast<int>(w.selected());
    if (w.step() != stepBefore) out.sound = UiSound::Confirm;
    if (w.step() == WizardStep::Network && !netRequested_) {
        netRequested_ = true;
        net_ = NetTest::Running;
        out.command = AppCommand::SetupNetTest;
    }
}

void SetupScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
    if (!wizard_) return;
    const SetupWizard& w = *wizard_;
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    const Rect card = cardRect(ctx);
    const float innerW = card.w - 2 * kPad;
    gfx::fillRect(dl, s, Rect{0, 0, ctx.width, ctx.height}, t.overlayDim);
    gfx::roundRect(dl, s, card, 16.0f, t.bg);

    const gfx::FontFace& head = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    const gfx::FontFace& bodyM = ctx.fonts->face(FontSize::Body, FontWeight::Medium);
    const gfx::FontFace& body = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    const gfx::FontFace& small = ctx.fonts->face(FontSize::Small, FontWeight::Regular);

    char title[64];
    std::snprintf(title, sizeof title, tr(Str::SetupStepOf), static_cast<int>(w.step()) + 1, kWizardStepCount);
    float y = card.y + kPad;
    gfx::drawTextEllipsis(dl, head, card.x + kPad, y + head.ascent, innerW, title, t.text);
    y += kTitleH;

    const StepText st = stepText(w.step());
    gfx::drawTextEllipsis(dl, bodyM, card.x + kPad, y + bodyM.ascent, innerW, tr(st.title), t.text);
    y += kHeadH;

    const std::string_view text = tr(st.text);
    std::array<gfx::Line, kTextLines> lines{};
    const int n = gfx::wrapText(small, text, innerW, lines);
    for (int i = 0; i < n; ++i) {
        const gfx::Line& l = lines[static_cast<std::size_t>(i)];
        gfx::drawTextEllipsis(dl, small, card.x + kPad, y + small.ascent, innerW, text.substr(l.start, l.length),
                              t.textMuted);
        y += kTextLineH;
    }
    y += 8.0f;

    const std::vector<std::string> opts = w.options(language() == Lang::De);
    const std::string suggested = std::string("  · ") + tr(Str::SetupSuggested);
    for (std::size_t i = 0; i < opts.size() && i < static_cast<std::size_t>(kMaxOptions); ++i) {
        const Rect row{card.x + kPad, y, innerW, kOptionH - 4.0f};
        const bool sel = i == w.selected();
        if (sel) gfx::roundRect(dl, s, row, 8.0f, t.accent);
        std::string label = opts[i];
        if (i == w.suggested() && opts.size() > 1) label += suggested;
        const float base = row.y + row.h / 2 + (body.ascent - body.descent) / 2;
        gfx::drawTextEllipsis(dl, body, row.x + 10.0f, base, row.w - 20.0f, label, sel ? t.onAccent : t.text);
        y += kOptionH;
    }

    if (w.step() == WizardStep::Network && net_ != NetTest::Idle) {
        y += 6.0f;
        gfx::drawTextEllipsis(dl, small, card.x + kPad, y + small.ascent, innerW, tr(netText(net_)),
                              net_ == NetTest::Fail ? t.danger : t.textMuted);
    }

    const float helpBase = card.y + card.h - kPad - small.descent;
    gfx::drawTextEllipsis(dl, small, card.x + kPad, helpBase, innerW, tr(Str::SetupHelp), t.textMuted);
}

}  // namespace nl::ui
