#include "ui/options_scene.hpp"

#include <cstdio>
#include <string>
#include <string_view>

#include "gfx/text.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;

constexpr float kCardMaxW = 520.0f, kPad = 16.0f, kTitleH = 32.0f, kRowH = 24.0f, kHelpH = 24.0f;  // 14 Zeilen in 480

bool contains(Rect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

std::string format(Str s, std::string_view arg) {
    char buf[160];
    std::snprintf(buf, sizeof buf, tr(s), std::string(arg).c_str());
    return buf;
}

struct KeyLabel {
    std::string_view key;
    Str label;
};
constexpr KeyLabel kLabels[] = {
    {"video", Str::OptVideo},           {"video_patch", Str::OptVideoPatch},
    {"vipatch", Str::OptVipatch},       {"language", Str::Language},
    {"country_patch", Str::OptCountryPatch}, {"region_patch", Str::OptRegionPatch},
    {"aspect", Str::OptAspect},         {"deflicker", Str::OptDeflicker},
    {"width", Str::OptWidth},           {"fix480p", Str::OptFix480p},
    {"online", Str::OptOnline},         {"ios", Str::OptIos},
    {"return_to", Str::OptReturnTo},    {"progressive", Str::OptProgressive},
    {"force_wide", Str::OptForceWide},  {"wiiu_wide", Str::OptWiiuWide},
    {"memcard", Str::OptMemcard},       {"memcard_blocks", Str::OptMemcardBlocks},
    {"native_si", Str::OptNativeSi},    {"max_pads", Str::OptMaxPads},
    {"skip_ipl", Str::OptSkipIpl},      {"cheats", Str::OptCheats},
    {"hook", Str::OptHook},
};

struct ValueLabel {
    std::string_view value;
    Str label;
};
constexpr ValueLabel kWords[] = {
    {"game", Str::ValGame},       {"system", Str::ValSystem},     {"auto", Str::LangAuto},
    {"none", Str::ValNoChange},   {"off", Str::Off},              {"0", Str::Off},
    {"1", Str::On},               {"normal", Str::ValNormal},     {"more", Str::ValMore},
    {"all", Str::ValAll},         {"off_ext", Str::ValOffExt},    {"low", Str::ValLow},
    {"medium", Str::ValMedium},   {"high", Str::ValHigh},         {"nossl", Str::ValNoSsl},
    {"emu", Str::ValEmulated},    {"multi", Str::ValEmulatedMulti}, {"real", Str::ValReal},
    {"ja", Str::ValLangJa},       {"en", Str::ValLangEn},         {"de", Str::ValLangDe},
    {"fr", Str::ValLangFr},       {"es", Str::ValLangEs},         {"it", Str::ValLangIt},
    {"nl", Str::ValLangNl},       {"zh_s", Str::ValLangZhS},      {"zh_t", Str::ValLangZhT},
    {"ko", Str::ValLangKo},
};

// Feste Schreibweisen ohne Übersetzung.
constexpr std::string_view kFixed[][2] = {
    {"pal50", "PAL 50"}, {"pal60", "PAL 60"},           {"ntsc", "NTSC"},       {"progressive", "480p"},
    {"mpal", "PAL-M"},   {"framebuffer", "Framebuffer"}, {"wiimmfi", "Wiimmfi"},
    {"vbi", "VBI"},      {"kpad", "KPAD"},              {"joypad", "Joypad"},   {"gxdraw", "GXDraw"},
    {"gxflush", "GXFlush"}, {"ossleep", "OSSleepThread"}, {"axframe", "AXNextFrame"},
};

std::string displayValue(std::string_view key, const std::string& v) {
    if (key == "max_pads") return v;
    if (key == "memcard_blocks") return format(Str::OptBlocks, v);
    if (key == "ios") return v == "auto" ? std::string(tr(Str::LangAuto)) : format(Str::OptCios, v);
    if (key == "return_to") {
        if (v == "none") return tr(Str::ValNone);
        if (v == "hbc") return tr(Str::HomebrewChannel);
        std::string up = v;  // Title-ID eines Kanals
        for (char& c : up)
            if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 'a' + 'A');
        return up;
    }
    for (const auto& f : kFixed)
        if (v == f[0]) return std::string(f[1]);
    for (const ValueLabel& w : kWords)
        if (v == w.value) return tr(w.label);
    return v;
}
}  // namespace

void OptionsScene::open(OptionsModel model, std::string title) {
    model_.emplace(std::move(model));
    title_ = std::move(title);
    closeRequested_ = false;
}

void OptionsScene::onEnter() {
    focus_ = 0;
    keysSincePointerMove_ = false;
}

std::string OptionsScene::titleText() const {
    if (!model_) return {};
    if (model_->perGame()) return title_.empty() ? model_->id6() : title_;
    return tr(model_->console() == Console::Gc ? Str::GcGames : Str::WiiGames);
}

std::string OptionsScene::labelText(int row) const {
    if (!model_ || row < 0 || row > static_cast<int>(model_->rows())) return {};
    if (isResetRow(row)) return tr(model_->perGame() ? Str::OptResetGame : Str::OptResetGlobal);
    const std::string_view key = model_->key(static_cast<std::size_t>(row));
    for (const KeyLabel& l : kLabels)
        if (key == l.key) return tr(l.label);
    return std::string(key);
}

std::string OptionsScene::valueText(int row) const {
    if (!model_ || row < 0 || row >= static_cast<int>(model_->rows())) return {};
    const std::size_t r = static_cast<std::size_t>(row);
    if (const std::optional<NinVersion> need = model_->requiredVersion(r))
        return format(Str::OptNeedsNin, formatNinVersion(*need));
    const std::string v = displayValue(model_->key(r), model_->shownValue(r));
    return model_->perGame() && model_->inherited(r) ? format(Str::OptDefault, v) : v;
}

Rect OptionsScene::cardRect(const UiContext& ctx) const {
    const float w = ctx.width - 64.0f < kCardMaxW ? ctx.width - 64.0f : kCardMaxW;
    const float h = kPad + kTitleH + static_cast<float>(rowCount()) * kRowH + kHelpH + kPad;
    return Rect{(ctx.width - w) / 2, (ctx.height - h) / 2, w, h};
}

Rect OptionsScene::rowRect(const UiContext& ctx, int row) const {
    const Rect c = cardRect(ctx);
    return Rect{c.x + kPad, c.y + kPad + kTitleH + static_cast<float>(row) * kRowH, c.w - 2 * kPad, kRowH};
}

int OptionsScene::hitTest(const UiContext& ctx, float x, float y) const {
    for (int i = 0; i < rowCount(); ++i)
        if (contains(rowRect(ctx, i), x, y)) return i;
    return -1;
}

void OptionsScene::update(const UiContext& ctx, const InputFrame& in, float, SceneRequests& out) {
    FocusSound sound(out, focus_);
    if (!model_) {
        if (in.pressed(Action::Back) || in.pressed(Action::Home)) out.pop = true;
        return;
    }

    const int hit = in.pointerValid ? hitTest(ctx, in.pointerX, in.pointerY) : -1;
    if (in.pointerValid && in.pointerMoved) {
        if (hit >= 0) focus_ = hit;
        keysSincePointerMove_ = false;
    }
    if (in.pressed(Action::Up) && focus_ > 0) --focus_;
    if (in.pressed(Action::Down) && focus_ + 1 < rowCount()) ++focus_;
    if (in.pressed(Action::Up) || in.pressed(Action::Down)) keysSincePointerMove_ = true;
    const bool pointerOff = in.pointerValid && hit < 0 && !keysSincePointerMove_;

    if (closeRequested_ || in.pressed(Action::Back) || in.pressed(Action::Home)) {
        closeRequested_ = false;
        out.pop = true;
        if (model_->dirty()) out.command = AppCommand::SaveOptions;
        return;
    }

    if (isResetRow(focus_)) {
        if (in.pressed(Action::Confirm) && !pointerOff) {
            model_->reset();
            out.sound = UiSound::Confirm;
        }
        return;
    }

    int dir = 0;
    if (in.pressed(Action::Right) || (in.pressed(Action::Confirm) && !pointerOff)) dir = 1;
    else if (in.pressed(Action::Left)) dir = -1;
    const std::size_t r = static_cast<std::size_t>(focus_);
    if (dir != 0 && model_->enabled(r)) {
        model_->step(r, dir);
        out.sound = UiSound::Move;
    }
}

void OptionsScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
    if (!model_) return;
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    const Rect card = cardRect(ctx);
    gfx::fillRect(dl, s, Rect{0, 0, ctx.width, ctx.height}, t.overlayDim);
    gfx::roundRect(dl, s, card, 16.0f, t.bg);

    const gfx::FontFace& head = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    gfx::drawTextEllipsis(dl, head, card.x + kPad, card.y + kPad + head.ascent, card.w - 2 * kPad, titleText(), t.text);

    const gfx::FontFace& label = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    const gfx::FontFace& value = ctx.fonts->face(FontSize::Body, FontWeight::Medium);
    const gfx::FontFace& small = ctx.fonts->face(FontSize::Small, FontWeight::Regular);
    for (int i = 0; i < rowCount(); ++i) {
        const Rect r = rowRect(ctx, i);
        const bool reset = isResetRow(i);
        const bool enabled = reset || model_->enabled(static_cast<std::size_t>(i));
        if (i == focus_) gfx::roundRect(dl, s, Rect{r.x - 4, r.y + 1, r.w + 8, r.h - 2}, 8, t.tileFocus);
        const float base = r.y + r.h / 2 + (label.ascent - label.descent) / 2;
        const Color fg = enabled ? t.text : t.textMuted;

        const std::string v = valueText(i);
        float vw = v.empty() ? 0.0f : gfx::measureText(value, v);
        const float maxV = r.w * 0.6f;
        if (vw > maxV) vw = maxV;
        const float labelMax = r.w - 16 - vw - (vw > 0 ? 12 : 0);
        gfx::drawTextEllipsis(dl, label, r.x + 8, base, labelMax > 0 ? labelMax : 0, labelText(i),
                              reset ? t.accent : fg);
        if (!v.empty()) gfx::drawTextEllipsis(dl, value, r.x + r.w - 8 - vw, base, vw, v, fg);
    }

    const float helpBase = card.y + card.h - kPad - small.descent;
    gfx::drawTextEllipsis(dl, small, card.x + kPad, helpBase, card.w - 2 * kPad, tr(Str::OptionsHelp), t.textMuted);
}

}  // namespace nl::ui
