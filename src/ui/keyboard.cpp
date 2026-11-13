#include "ui/keyboard.hpp"

#include "gfx/text.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

namespace {
using gfx::Rect;

constexpr const char* kRows[5] = {"1234567890", "QWERTZUIOP", "ASDFGHJKL", "YXCVBNM", "\x01\x02\x03"};
constexpr int kRowCount = 5;
constexpr float kKeyW = 44.0f, kKeyH = 40.0f, kGap = 6.0f, kInfoBarH = 72.0f;
constexpr float kSpecialW[3] = {180.0f, 120.0f, 120.0f};
constexpr float kFilterH = 36.0f;

int rowLength(int r) {
    int n = 0;
    while (kRows[r][n] != '\0') ++n;
    return n;
}

int rowStart(int r) {
    int start = 0;
    for (int i = 0; i < r; ++i) start += rowLength(i);
    return start;
}

char keyChar(int index) {
    for (int r = 0; r < kRowCount; ++r) {
        if (index < rowLength(r)) return kRows[r][index];
        index -= rowLength(r);
    }
    return '\0';
}

float rowWidth(int r) {
    if (r == kRowCount - 1) return kSpecialW[0] + kSpecialW[1] + kSpecialW[2] + 2 * kGap;
    return static_cast<float>(rowLength(r)) * (kKeyW + kGap) - kGap;
}

bool contains(Rect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

float panelTop(const UiContext& ctx) { return ctx.height - kInfoBarH - kRowCount * (kKeyH + kGap) - 64.0f; }
}  // namespace

int Keyboard::keyCount() const { return rowStart(kRowCount); }

int Keyboard::keyIndex(char key) const {
    for (int i = 0; i < keyCount(); ++i)
        if (keyChar(i) == key) return i;
    return -1;
}

int Keyboard::row(int key) const {
    for (int r = 0; r < kRowCount; ++r)
        if (key < rowStart(r) + rowLength(r)) return r;
    return kRowCount - 1;
}

Rect Keyboard::keyRect(const UiContext& ctx, int index) const {
    const int r = row(index);
    const int col = index - rowStart(r);
    const float x0 = (ctx.width - rowWidth(r)) / 2;
    const float y = panelTop(ctx) + 52.0f + static_cast<float>(r) * (kKeyH + kGap);
    if (r == kRowCount - 1) {
        float x = x0;
        for (int i = 0; i < col; ++i) x += kSpecialW[i] + kGap;
        return Rect{x, y, kSpecialW[col], kKeyH};
    }
    return Rect{x0 + static_cast<float>(col) * (kKeyW + kGap), y, kKeyW, kKeyH};
}

Rect Keyboard::filterRect(const UiContext& ctx) const {
    const float w = ctx.width / 2;
    return Rect{(ctx.width - w) / 2, panelTop(ctx) - 8 - kFilterH, w, kFilterH};
}

void Keyboard::onEnter() {
    text_.clear();
    focus_ = 0;
    filterRow_ = false;
    keysSincePointerMove_ = false;
}

void Keyboard::stepFilter(int dir, SceneRequests& out) {
    filter_->setFilterIndex(stepFilterIndex(filter_->filterIndex(), dir, filter_->filterCount()));
    out.sound = UiSound::Move;
}

void Keyboard::press(int key, SceneRequests& out) {
    const char c = keyChar(key);
    if (c == kDone) {
        out.pop = true;
        return;
    }
    if (c == kBackspace) {
        if (text_.empty()) return;
        text_.pop_back();
    } else {
        if (text_.size() >= kMaxText) return;
        text_ += c == kSpace ? ' ' : c;
    }
    target_.setQuery(text_);
}

void Keyboard::update(const UiContext& ctx, const InputFrame& in, float, SceneRequests& out) {
    FocusSound sound(out, focus_);
    const bool filterOn = hasFilter();
    if (!filterOn) filterRow_ = false;
    int hit = -1;
    bool hitFilter = false;
    if (in.pointerValid) {
        for (int i = 0; i < keyCount() && hit < 0; ++i)
            if (contains(keyRect(ctx, i), in.pointerX, in.pointerY)) hit = i;
        hitFilter = filterOn && contains(filterRect(ctx), in.pointerX, in.pointerY);
        if (in.pointerMoved) {
            if (hit >= 0) {
                focus_ = hit;
                filterRow_ = false;
            } else if (hitFilter && !filterRow_) {
                filterRow_ = true;
                out.sound = UiSound::Move;  // FocusSound sieht nur focus_
            }
            keysSincePointerMove_ = false;
        }
    }
    const int r = row(focus_);
    const bool wasFilterRow = filterRow_;
    if (filterRow_) {
        if (in.pressed(Action::Left)) stepFilter(-1, out);
        if (in.pressed(Action::Right)) stepFilter(1, out);
        if (in.pressed(Action::Down)) {
            filterRow_ = false;  // zurück auf die zuletzt gewählte Taste
            out.sound = UiSound::Move;
        }
    } else if (in.pressed(Action::Up) && r == 0 && filterOn) {
        filterRow_ = true;
        out.sound = UiSound::Move;
    } else {
        if (in.pressed(Action::Left) && focus_ > rowStart(r)) --focus_;
        if (in.pressed(Action::Right) && focus_ < rowStart(r) + rowLength(r) - 1) ++focus_;
    }
    if (!wasFilterRow && !filterRow_ && (in.pressed(Action::Up) || in.pressed(Action::Down))) {
        const int target = in.pressed(Action::Up) ? r - 1 : r + 1;
        if (target >= 0 && target < kRowCount) {
            const Rect cur = keyRect(ctx, focus_);
            const float cx = cur.x + cur.w / 2;
            int best = rowStart(target);
            float bestD = 1e9f;
            for (int i = rowStart(target); i < rowStart(target) + rowLength(target); ++i) {
                const Rect k = keyRect(ctx, i);
                const float d = k.x + k.w / 2 - cx;
                if ((d < 0 ? -d : d) < bestD) {
                    bestD = d < 0 ? -d : d;
                    best = i;
                }
            }
            focus_ = best;
        }
    }
    if (in.pressed(Action::Left) || in.pressed(Action::Right) || in.pressed(Action::Up) || in.pressed(Action::Down))
        keysSincePointerMove_ = true;

    if (in.pressed(Action::Back) || in.pressed(Action::Home)) {
        out.pop = true;
        return;
    }
    const bool pointerOff = in.pointerValid && hit < 0 && !hitFilter && !keysSincePointerMove_;
    if (in.pressed(Action::Confirm) && !pointerOff) {
        if (filterRow_) {
            stepFilter(1, out);  // A blättert weiter, die Tastatur bleibt offen
            return;
        }
        press(focus_, out);
        out.sound = UiSound::Confirm;
    }
}

void Keyboard::draw(const UiContext& ctx, gfx::DrawList& dl) {
    const Theme& t = *ctx.theme;
    const gfx::AtlasShapes& s = *ctx.shapes;
    const float top = panelTop(ctx);
    gfx::fillRect(dl, s, Rect{0, top, ctx.width, ctx.height - kInfoBarH - top}, t.bar);
    gfx::fillRect(dl, s, Rect{0, top, ctx.width, 1}, t.barBorder);

    // Eingabefeld mit dem bisherigen Text
    const gfx::FontFace& title = ctx.fonts->face(gfx::FontSize::Title, gfx::FontWeight::Medium);
    const Rect field{(ctx.width - 420) / 2, top + 8, 420, 36};
    gfx::roundRect(dl, s, field, 10, t.bg);
    const char* shown = text_.empty() ? tr(Str::SearchHint) : text_.c_str();
    gfx::drawTextEllipsis(dl, title, field.x + 12, field.y + field.h / 2 + (title.ascent - title.descent) / 2, field.w - 24,
                          shown, text_.empty() ? t.textMuted : t.text);

    const gfx::FontFace& f = ctx.fonts->face(gfx::FontSize::Body, gfx::FontWeight::Medium);
    if (hasFilter()) {  // wie in der A–Z-Leiste
        const Rect r = filterRect(ctx);
        gfx::roundRect(dl, s, r, 10, filterRow_ ? t.accent : t.bar);
        const std::string label =
            std::string(tr(Str::Filter)) + ":  < " + filter_->filterLabel(filter_->filterIndex()) + " >";
        const float w = gfx::measureText(f, label.c_str());
        const float x = w < r.w - 16 ? r.x + (r.w - w) / 2 : r.x + 8;
        gfx::drawTextEllipsis(dl, f, x, r.y + r.h / 2 + (f.ascent - f.descent) / 2, r.w - 16, label.c_str(),
                              filterRow_ ? t.onAccent : t.text);
    }
    for (int i = 0; i < keyCount(); ++i) {
        const Rect r = keyRect(ctx, i);
        const bool focused = i == focus_ && !filterRow_;
        gfx::roundRect(dl, s, r, 8, focused ? t.accent : t.tile);
        const char c = keyChar(i);
        char single[2] = {c, '\0'};
        const char* label = c == kSpace ? tr(Str::KeySpace) : c == kBackspace ? tr(Str::KeyDelete) : c == kDone ? tr(Str::KeyDone) : single;
        const float w = gfx::measureText(f, label);
        gfx::drawTextEllipsis(dl, f, w < r.w - 8 ? r.x + (r.w - w) / 2 : r.x + 4, r.y + r.h / 2 + (f.ascent - f.descent) / 2,
                              r.w - 8, label, focused ? t.onAccent : t.text);
    }
}

}  // namespace nl::ui
