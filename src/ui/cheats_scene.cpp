#include "ui/cheats_scene.hpp"

#include "gfx/text.hpp"

namespace nl::ui {

namespace {
using gfx::FontSize;
using gfx::FontWeight;
using gfx::Rect;

constexpr float kHeaderH = 64.0f, kFooterH = 56.0f, kMargin = 32.0f;
constexpr float kListTop = kHeaderH + 3.0f + 16.0f, kRowH = 36.0f, kRowGap = 4.0f, kBox = 16.0f;

// "%s" ersetzen ohne festen Puffer: ein langer Spieltitel darf nicht mitten im UTF-8-Zeichen abreißen.
std::string format(Str s, const std::string& arg) {
    std::string out = tr(s);
    const std::size_t at = out.find("%s");
    if (at != std::string::npos) out.replace(at, 2, arg);
    return out;
}
}  // namespace

void CheatsScene::open(CheatModel model, std::string id6, std::string title, bool fileExists, std::optional<Str> fetchBlocked) {
    model_.emplace(std::move(model));
    id6_ = std::move(id6);
    title_ = std::move(title);
    fileExists_ = fileExists;
    fetchBlocked_ = fetchBlocked;
    message_.reset();
    fetching_ = overwrite_ = confirm_ = closeRequested_ = false;
    focus_ = scroll_ = 0;
}

void CheatsScene::fetchDone(CheatFetchResult result, std::optional<CheatModel> reloaded) {
    fetching_ = false;
    message_.reset();
    switch (result) {
        case CheatFetchResult::Ok:
        case CheatFetchResult::Exists:
            if (reloaded) {
                model_.emplace(std::move(*reloaded));
                fileExists_ = true;
                focus_ = scroll_ = 0;
            }
            break;
        case CheatFetchResult::NotFound:
        case CheatFetchResult::NoCodes: message_ = Str::CheatsNoCodes; break;
        case CheatFetchResult::NetError: message_ = Str::CheatsNetError; break;
    }
}

std::string CheatsScene::statusText() const {
    if (!model_) return {};
    if (model_->status() == CheatStatus::WiiUnavailable) return tr(Str::CheatsWiiUnavailable);
    if (confirm_) return tr(Str::CheatsOverwrite);
    if (fetching_) return tr(Str::CheatsLoading);
    if (message_) return tr(*message_);
    if (model_->status() == CheatStatus::NoFile) return tr(Str::CheatsNoFile);
    return {};
}

std::string CheatsScene::rowText(int row) const {
    if (!model_ || row < 0 || row >= static_cast<int>(model_->rows())) return {};
    const std::size_t r = static_cast<std::size_t>(row);
    if (!model_->selectable(r)) return tr(Str::CheatsUnselectable);
    return model_->author(r);
}

void CheatsScene::clampScroll() {
    if (focus_ < scroll_) scroll_ = focus_;
    if (focus_ >= scroll_ + kVisibleRows) scroll_ = focus_ - kVisibleRows + 1;
}

void CheatsScene::update(const UiContext&, const InputFrame& in, float, SceneRequests& out) {
    FocusSound sound(out, focus_);
    if (!model_) {
        if (in.pressed(Action::Back) || in.pressed(Action::Home)) out.pop = true;
        return;
    }
    if (confirm_) {  // Nachfrage „ersetzen?“: nur A oder B
        if (in.pressed(Action::Confirm)) {
            confirm_ = false;
            fetching_ = true;
            overwrite_ = true;
            message_.reset();
            out.command = AppCommand::FetchCheats;
        } else if (in.pressed(Action::Back) || in.pressed(Action::Home)) {
            confirm_ = false;
            out.sound = UiSound::Back;
        }
        return;
    }
    if (closeRequested_ || in.pressed(Action::Back) || in.pressed(Action::Home)) {
        closeRequested_ = false;
        out.pop = true;
        if (model_->dirty()) out.command = AppCommand::SaveCheats;
        return;
    }
    const bool wiiOff = model_->status() == CheatStatus::WiiUnavailable;
    if (in.pressed(Action::Options) && !fetching_ && !wiiOff) {
        if (fetchBlocked_) {
            message_ = fetchBlocked_;
        } else if (fileExists_ || model_->status() == CheatStatus::Ready) {
            confirm_ = true;
            out.sound = UiSound::Open;
        } else {
            fetching_ = true;
            overwrite_ = false;
            message_.reset();
            out.command = AppCommand::FetchCheats;
        }
        return;
    }

    const int n = static_cast<int>(model_->rows());
    if (n == 0) return;
    if (in.pressed(Action::Up) && focus_ > 0) --focus_;
    if (in.pressed(Action::Down) && focus_ + 1 < n) ++focus_;
    if (in.pressed(Action::Left)) focus_ = focus_ > kVisibleRows ? focus_ - kVisibleRows : 0;
    if (in.pressed(Action::Right)) focus_ = focus_ + kVisibleRows < n ? focus_ + kVisibleRows : n - 1;
    clampScroll();
    if (in.pressed(Action::Confirm) && model_->toggle(static_cast<std::size_t>(focus_))) out.sound = UiSound::Confirm;
}

void CheatsScene::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    gfx::fillRect(dl, s, Rect{0, 0, ctx.width, ctx.height}, t.bg);

    gfx::fillRect(dl, s, Rect{0, 0, ctx.width, kHeaderH}, t.bar);
    gfx::fillRect(dl, s, Rect{0, kHeaderH, ctx.width, 3}, t.accent);
    const gfx::FontFace& head = ctx.fonts->face(FontSize::Title, FontWeight::Medium);
    const float w = ctx.width - 2 * kMargin;
    gfx::drawTextEllipsis(dl, head, kMargin, kHeaderH / 2 + (head.ascent - head.descent) / 2, w,
                          format(Str::CheatsTitle, title_.empty() ? id6_ : title_), t.text);
    if (!model_) return;

    const gfx::FontFace& body = ctx.fonts->face(FontSize::Body, FontWeight::Regular);
    float y = kListTop;
    const std::string status = statusText();
    if (!status.empty()) {
        gfx::drawTextEllipsis(dl, body, kMargin, y + body.ascent, w, status, confirm_ ? t.accent : t.text2);
        y += body.lineHeight + 8.0f;
    }
    const int n = model_->status() == CheatStatus::WiiUnavailable ? 0 : static_cast<int>(model_->rows());
    const int end = n < scroll_ + kVisibleRows ? n : scroll_ + kVisibleRows;
    const float bottom = ctx.height - kFooterH - 4.0f;
    for (int i = scroll_; i < end && y + kRowH <= bottom; ++i) {
        const std::size_t r = static_cast<std::size_t>(i);
        const Rect row{kMargin, y, w, kRowH};
        const bool focused = i == focus_;
        const bool on = model_->selectable(r);
        if (focused) gfx::roundRect(dl, s, Rect{row.x - 3, row.y - 3, row.w + 6, row.h + 6}, kRowH / 2 + 3, t.accent);
        gfx::roundRect(dl, s, row, kRowH / 2, focused ? t.tileFocus : t.tile);
        const Rect box{row.x + 14, row.y + (kRowH - kBox) / 2, kBox, kBox};
        gfx::roundRect(dl, s, box, 4, on ? t.textMuted : t.tile);
        if (model_->checked(r)) gfx::roundRect(dl, s, Rect{box.x + 3, box.y + 3, kBox - 6, kBox - 6}, 2, t.accent);
        const float base = row.y + row.h / 2 + (body.ascent - body.descent) / 2;
        const std::string right = rowText(i);
        float rw = right.empty() ? 0.0f : gfx::measureText(body, right);
        if (rw > w * 0.35f) rw = w * 0.35f;
        const float textX = box.x + kBox + 12;
        const float leftW = row.x + row.w - 16 - rw - (rw > 0 ? 12 : 0) - textX;
        gfx::drawTextEllipsis(dl, body, textX, base, leftW > 0 ? leftW : 0, model_->label(r), on ? t.text : t.textMuted);
        if (!right.empty()) gfx::drawTextEllipsis(dl, body, row.x + row.w - 16 - rw, base, rw, right, t.text2);
        y += kRowH + kRowGap;
    }

    const float top = ctx.height - kFooterH;
    gfx::fillRect(dl, s, Rect{0, top, ctx.width, kFooterH}, t.bar);
    gfx::fillRect(dl, s, Rect{0, top, ctx.width, 1}, t.barBorder);
    gfx::drawTextEllipsis(dl, body, kMargin, top + kFooterH / 2 + (body.ascent - body.descent) / 2, w, tr(Str::CheatsHelp), t.text2);
}

}  // namespace nl::ui
