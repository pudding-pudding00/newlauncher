#include "ui/scene.hpp"

namespace nl::ui {

const char* appCommandName(AppCommand c) {
    switch (c) {
        case AppCommand::ExitToHbc: return "exit_hbc";
        case AppCommand::WiiMenu: return "wii_menu";
        case AppCommand::PowerOff: return "power_off";
        case AppCommand::CycleTheme: return "cycle_theme";
        case AppCommand::ToggleFavorite: return "toggle_favorite";
        case AppCommand::Launch: return "launch";
        case AppCommand::SetN64Method: return "set_n64_method";
        case AppCommand::OpenVcMap: return "open_vc_map";
        case AppCommand::SaveVcMap: return "save_vc_map";
        case AppCommand::FetchCovers: return "fetch_covers";
        case AppCommand::SettingsChanged: return "settings_changed";
        case AppCommand::SaveSettings: return "save_settings";
        case AppCommand::Rescan: return "rescan";
        case AppCommand::OpenEmulators: return "open_emulators";
        case AppCommand::OpenAbout: return "open_about";
        case AppCommand::OpenSysInfo: return "open_sysinfo";
        case AppCommand::SaveSysInfo: return "save_sysinfo";
        case AppCommand::EmuFetchList: return "emu_fetch_list";
        case AppCommand::EmuInstall: return "emu_install";
        case AppCommand::EmuInstallAll: return "emu_install_all";
        case AppCommand::EmuCancel: return "emu_cancel";
        case AppCommand::None: break;
    }
    return "none";
}

bool SceneStack::contains(const Scene* s) const {
    if (s == nullptr) return false;
    if (pushAfterPop_ == s) return true;
    for (int i = 0; i < depth_; ++i)
        if (entries_[i].scene == s && !entries_[i].popping) return true;  // ausblendend zählt nicht
    return false;
}

void SceneStack::push(Scene* s) {
    if (s == nullptr || contains(s)) return;  // dieselbe Szene zweimal hieße doppelt B drücken
    if (depth_ > 0 && entries_[depth_ - 1].popping) {  // erst ausblenden lassen, dann öffnen
        pushAfterPop_ = s;
        return;
    }
    if (depth_ >= kMaxDepth) return;
    Entry& e = entries_[depth_++];
    e.scene = s;
    e.popping = false;
    if (s->isOverlay()) {
        e.alpha.snap(0.0f);
        e.alpha.to(1.0f, kOverlayFadeMs);
    } else {
        e.alpha.snap(1.0f);
    }
    s->onEnter();
}

int SceneStack::lowestVisible() const {
    int i = depth_ - 1;
    while (i > 0 && entries_[i].scene->isOverlay()) --i;
    return i;
}

AppCommand SceneStack::update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests* topRequests) {
    if (topRequests != nullptr) *topRequests = SceneRequests{};
    for (int i = 0; i < depth_; ++i) entries_[i].alpha.update(dtMs);
    if (depth_ > 0 && entries_[depth_ - 1].popping && entries_[depth_ - 1].alpha.done()) {
        entries_[--depth_] = Entry{};
        if (pushAfterPop_ != nullptr) {
            Scene* next = pushAfterPop_;
            pushAfterPop_ = nullptr;  // vor push, sonst hält contains() sie für schon vorhanden
            push(next);
        }
    }
    if (depth_ == 0) return AppCommand::None;

    const InputFrame none{};
    const bool topPopping = entries_[depth_ - 1].popping;
    SceneRequests topReq{};
    for (int i = lowestVisible(); i < depth_; ++i) {
        const bool isTop = i == depth_ - 1;
        SceneRequests req{};
        entries_[i].scene->update(ctx, (isTop && !topPopping) ? in : none, dtMs, req);
        if (isTop) topReq = req;
    }
    if (topPopping) return AppCommand::None;
    if (topRequests != nullptr) *topRequests = topReq;

    if (topReq.pop && depth_ > 1) {
        Entry& e = entries_[depth_ - 1];
        if (e.scene->isOverlay()) {
            e.popping = true;
            e.alpha.to(0.0f, kOverlayFadeMs);
            pushAfterPop_ = topReq.push;
        } else {
            entries_[--depth_] = Entry{};
            if (topReq.push != nullptr) push(topReq.push);
        }
    } else if (topReq.push != nullptr) {
        push(topReq.push);
    }
    return topReq.command;
}

void SceneStack::draw(const UiContext& ctx, gfx::DrawList& dl) {
    if (depth_ == 0) return;
    for (int i = lowestVisible(); i < depth_; ++i) {
        Entry& e = entries_[i];
        if (e.scene->isOverlay()) {
            dl.pushAlpha(e.alpha.value());
            if (ctx.theme != nullptr && ctx.shapes != nullptr) {
                gfx::fillRect(dl, *ctx.shapes, gfx::Rect{0, 0, ctx.width, ctx.height}, ctx.theme->overlayDim);
            }
            e.scene->draw(ctx, dl);
            dl.popAlpha();
        } else {
            e.scene->draw(ctx, dl);
        }
    }
}

}  // namespace nl::ui
