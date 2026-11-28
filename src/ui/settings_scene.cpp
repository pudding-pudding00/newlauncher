#include "ui/settings_scene.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

#include "gfx/text.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;

constexpr float kCardMaxW = 480.0f, kPad = 16.0f, kPadY = 4.0f, kTitleH = 22.0f, kRowH = 16.0f, kHelpH = 18.0f;  // 27 Zeilen in 480 (M24)
constexpr int kVisibleRows = 27;  // M27: mehr Zeilen → die Liste scrollt mit dem Fokus

const Str kLabels[SettingsScene::kCount] = {
    Str::Language, Str::Appearance, Str::DarkFrom,      Str::DarkTo, Str::ThemePack,  Str::Music,       Str::Volume,
    Str::Sounds,   Str::Banners, Str::LibraryDevice, Str::SortOrder, Str::CoverRegion, Str::CoverAuto, Str::CoverStorage, Str::Rescan, Str::FetchCovers, Str::ClearCovers, Str::VcMapTitle,
    Str::Emulators, Str::WiiGames, Str::GcGames, Str::Cloud, Str::Parental, Str::WiiloadSetting, Str::TransferSetting, Str::Setup, Str::SysInfo, Str::About, Str::SelfUpdate, Str::RestartSelf,
};

bool contains(Rect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

bool sameSettings(const Config& a, const Config& b) {
    return a.language == b.language && a.library == b.library && a.libraryRules.sources == b.libraryRules.sources &&
           a.usbTimeoutSec == b.usbTimeoutSec &&
           a.themeMode == b.themeMode && a.themePack == b.themePack && a.darkFromMin == b.darkFromMin && a.darkToMin == b.darkToMin &&
           a.music == b.music && a.musicVolume == b.musicVolume && a.sounds == b.sounds &&
           a.banners == b.banners &&
           a.coverRegion == b.coverRegion && a.coverAuto == b.coverAuto && a.sortOrder == b.sortOrder &&
           a.wiiloadMode == b.wiiloadMode;
}

AppCommand actionCommand(SettingRow r) {
    switch (r) {
        case SettingRow::Rescan: return AppCommand::Rescan;
        case SettingRow::FetchCovers: return AppCommand::FetchCovers;
        case SettingRow::ClearCovers: return AppCommand::ClearCovers;
        case SettingRow::VcMap: return AppCommand::OpenVcMap;
        case SettingRow::Emulators: return AppCommand::OpenEmulators;
        case SettingRow::WiiGames:
        case SettingRow::GcGames: return AppCommand::OpenOptions;
        case SettingRow::Cloud: return AppCommand::OpenCloud;
        case SettingRow::Parental: return AppCommand::OpenParental;
        case SettingRow::Transfer: return AppCommand::OpenTransfer;
        case SettingRow::Setup: return AppCommand::OpenSetup;
        case SettingRow::SysInfo: return AppCommand::OpenSysInfo;
        case SettingRow::About: return AppCommand::OpenAbout;
        case SettingRow::SelfUpdate: return AppCommand::SelfUpdate;
        case SettingRow::Restart: return AppCommand::RestartSelf;
        default: return AppCommand::None;
    }
}

std::string valueText(const Config& c, SettingRow r) {
    switch (r) {
        case SettingRow::Language:
            return tr(c.language == Config::Language::De   ? Str::LangDe
                      : c.language == Config::Language::En ? Str::LangEn
                                                           : Str::LangAuto);
        case SettingRow::Theme:
            return tr(c.themeMode == ThemeMode::Dark   ? Str::ThemeDark
                      : c.themeMode == ThemeMode::Auto ? Str::ThemeAuto
                                                       : Str::ThemeLight);
        case SettingRow::DarkFrom: return formatClock(c.darkFromMin);
        case SettingRow::DarkTo: return formatClock(c.darkToMin);
        case SettingRow::Music: return tr(c.music ? Str::On : Str::Off);
        case SettingRow::Volume: {
            char buf[16];
            std::snprintf(buf, sizeof buf, "%d %%", c.musicVolume);
            return buf;
        }
        case SettingRow::Sounds: return tr(c.sounds ? Str::On : Str::Off);
        case SettingRow::Banners: return tr(c.banners ? Str::On : Str::Off);
        case SettingRow::CoverAuto: return tr(c.coverAuto ? Str::On : Str::Off);
        case SettingRow::CoverStorage:
            return tr(c.coverStorage == CoverStorage::Device ? Str::CoverStorageDevice : Str::CoverStorageSd);
        case SettingRow::Library:
            switch (sourceChoice(c.libraryRules.sources)) {
                case SourceChoice::Auto: return std::string(tr(Str::LangAuto));
                case SourceChoice::Usb: return std::string(tr(Str::DeviceUsb));
                case SourceChoice::Sd: return std::string(tr(Str::DeviceSd));
                case SourceChoice::Custom: break;
            }
            return sourcesToText(c.libraryRules.sources);
        case SettingRow::Sort: {
            constexpr Str kNames[] = {Str::SortTitle, Str::SortYear, Str::SortRecent, Str::SortMost};
            return tr(kNames[static_cast<int>(c.sortOrder) & 3]);
        }
        case SettingRow::CoverRegion: return c.coverRegion == "auto" ? std::string(tr(Str::LangAuto)) : c.coverRegion;
        case SettingRow::Wiiload:
            return tr(c.wiiloadMode == WiiloadMode::Confirm ? Str::WiiloadConfirm
                      : c.wiiloadMode == WiiloadMode::Auto  ? Str::WiiloadAuto
                                                            : Str::WiiloadOff);
        default: return {};
    }
}
}  // namespace

std::string SettingsScene::valueTextFor(SettingRow row) const {
    if (row == SettingRow::ClearCovers) return holding_ ? std::string(tr(Str::ClearCoversHold)) : std::string();
    if (live_ == nullptr) return {};
    if (row == SettingRow::ThemePack) return live_->themePack.empty() ? std::string(tr(Str::ThemePackDefault)) : live_->themePack;
    return valueText(*live_, row);
}

void SettingsScene::onEnter() {
    focus_ = 0;
    keysSincePointerMove_ = false;
    pendingAction_ = AppCommand::None;
    holding_ = false;
    holdMs_ = 0.0f;
    if (live_ != nullptr) saved_ = *live_;
}

bool SettingsScene::dirty() const { return live_ != nullptr && !sameSettings(*live_, saved_); }

Rect SettingsScene::cardRect(const UiContext& ctx) const {
    const float w = ctx.width - 64.0f < kCardMaxW ? ctx.width - 64.0f : kCardMaxW;
    const int rows = kCount < kVisibleRows ? kCount : kVisibleRows;
    const float h = kPadY + kTitleH + static_cast<float>(rows) * kRowH + kHelpH + kPadY;
    return Rect{(ctx.width - w) / 2, (ctx.height - h) / 2, w, h};
}

Rect SettingsScene::rowRect(const UiContext& ctx, int row) const {
    const Rect c = cardRect(ctx);
    return Rect{c.x + kPad, c.y + kPadY + kTitleH + static_cast<float>(row - scroll_) * kRowH, c.w - 2 * kPad, kRowH};
}

int SettingsScene::hitTest(const UiContext& ctx, float x, float y) const {
    for (int i = scroll_; i < kCount && i < scroll_ + kVisibleRows; ++i)
        if (contains(rowRect(ctx, i), x, y)) return i;
    return -1;
}

void SettingsScene::update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) {
    FocusSound sound(out, focus_);
    if (pendingAction_ != AppCommand::None) {  // SaveSettings lief im letzten Frame, der Klang auch
        sound.silence();
        out.command = pendingAction_;
        out.console = pendingConsole_;
        out.pop = true;
        pendingAction_ = AppCommand::None;
        return;
    }
    if (live_ == nullptr) {
        if (in.pressed(Action::Back) || in.pressed(Action::Home)) out.pop = true;
        return;
    }

    const int hit = in.pointerValid ? hitTest(ctx, in.pointerX, in.pointerY) : -1;
    if (in.pointerValid && in.pointerMoved) {
        if (hit >= 0) focus_ = hit;
        keysSincePointerMove_ = false;
    }
    if (in.pressed(Action::Up) && focus_ > 0) --focus_;
    if (in.pressed(Action::Down) && focus_ + 1 < kCount) ++focus_;
    if (in.pressed(Action::Up) || in.pressed(Action::Down)) keysSincePointerMove_ = true;
    if (focus_ < scroll_) scroll_ = focus_;
    if (focus_ >= scroll_ + kVisibleRows) scroll_ = focus_ - kVisibleRows + 1;
    // M24: Loslassen und Fokuswechsel brechen das Halten ab.
    if (focus_ != static_cast<int>(SettingRow::ClearCovers) || !in.confirmHeld) holding_ = false;
    if (!holding_) holdMs_ = 0.0f;
    const bool pointerOff = in.pointerValid && hit < 0 && !keysSincePointerMove_;

    if (in.pressed(Action::Back) || in.pressed(Action::Home)) {
        out.pop = true;
        holding_ = false;
        holdMs_ = 0.0f;
        if (dirty()) {
            out.command = AppCommand::SaveSettings;
            saved_ = *live_;
        }
        return;
    }

    const SettingRow row = static_cast<SettingRow>(focus_);
    if (isAction(row)) {
        bool fire = in.pressed(Action::Confirm) && !pointerOff;
        if (row == SettingRow::ClearCovers) {  // M24: nur nach 1 s Halten; Start nur mit frischem Druck
            if (fire && in.confirmHeld) {
                holding_ = true;
                holdMs_ = 0.0f;
                out.sound = UiSound::Move;
                return;
            }
            if (holding_) holdMs_ += dtMs;
            fire = holding_ && holdMs_ >= kHoldConfirmMs;
            if (fire) {
                holding_ = false;
                holdMs_ = 0.0f;
            }
        }
        if (fire) {
            out.console = row == SettingRow::GcGames ? Console::Gc : Console::Wii;
            pendingConsole_ = out.console;
            if (dirty()) {
                out.command = AppCommand::SaveSettings;
                saved_ = *live_;
                pendingAction_ = actionCommand(row);
            } else {
                out.command = actionCommand(row);
                out.pop = true;
            }
            out.sound = UiSound::Confirm;
        }
        return;
    }

    int dir = 0;
    if (in.pressed(Action::Right) || (in.pressed(Action::Confirm) && !pointerOff)) dir = 1;
    else if (in.pressed(Action::Left)) dir = -1;
    if (dir != 0 && settingEnabled(*live_, row)) {
        if (row == SettingRow::ThemePack) stepThemePack(*live_, themePacks_, dir);
        else stepSetting(*live_, row, dir);
        out.command = AppCommand::SettingsChanged;
    }
}

void SettingsScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    const Rect card = cardRect(ctx);
    gfx::fillRect(dl, s, Rect{0, 0, ctx.width, ctx.height}, t.overlayDim);
    gfx::roundRect(dl, s, card, 16.0f, t.bg);

    const gfx::FontFace& head = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    gfx::drawTextEllipsis(dl, head, card.x + kPad, card.y + kPadY + head.ascent, card.w - 2 * kPad, tr(Str::Settings),
                          t.text);

    const gfx::FontFace& label = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    const gfx::FontFace& value = ctx.fonts->face(FontSize::Body, FontWeight::Medium);
    const gfx::FontFace& small = ctx.fonts->face(FontSize::Small, FontWeight::Regular);
    for (int i = scroll_; i < kCount && i < scroll_ + kVisibleRows; ++i) {
        const Rect r = rowRect(ctx, i);
        const SettingRow row = static_cast<SettingRow>(i);
        const bool enabled = live_ == nullptr || settingEnabled(*live_, row);
        if (i == focus_) gfx::roundRect(dl, s, Rect{r.x - 4, r.y + 1, r.w + 8, r.h - 2}, 8, t.tileFocus);
        const float base = r.y + r.h / 2 + (label.ascent - label.descent) / 2;
        const Color fg = enabled ? t.text : t.textMuted;

        std::string v = valueTextFor(row);
        const float vw = v.empty() ? 0.0f : gfx::measureText(value, v);
        float labelMax = r.w - 16 - vw - (vw > 0 ? 12 : 0);
        if (row == SettingRow::Library && restartHintShown()) {
            const std::string_view hint = tr(Str::TakesEffectAfterRestart);
            const float hw = gfx::measureText(small, hint);
            gfx::drawText(dl, small, r.x + r.w - 8 - vw - 10 - hw, base, hint, t.textMuted);
            labelMax -= hw + 10;
        }
        gfx::drawTextEllipsis(dl, label, r.x + 8, base, labelMax > 0 ? labelMax : 0, tr(kLabels[i]),
                              isAction(row) ? t.accent : fg);
        if (!v.empty()) gfx::drawText(dl, value, r.x + r.w - 8 - vw, base, v, fg);
        if (row == SettingRow::ClearCovers && holding_) {  // M24: Fortschritt beim Halten
            const float p = std::min(holdProgress(), 1.0f);
            gfx::fillRect(dl, s, Rect{r.x, r.y + r.h - 3, r.w * p, 2}, t.accent);
        }
    }

    const float helpBase = card.y + card.h - kPadY - small.descent;
    gfx::drawTextEllipsis(dl, small, card.x + kPad, helpBase, card.w - 2 * kPad, tr(Str::SettingsHelp), t.textMuted);
}

}  // namespace nl::ui
