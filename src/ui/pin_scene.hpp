#pragma once
// PIN-Abfrage der Kindersicherung (M17, Spec v2 10.5): vier Ziffern mit dem Steuerkreuz.
// ▲▼ ändert die Ziffer, ◀▶ die Stelle, A geht weiter und bestätigt auf der letzten Stelle, B bricht ab.
// Verify meldet PinEntered; main prüft (PinGate) und ruft accept() oder reject(). Set fragt zweimal und meldet
// PinEntered erst, wenn beide Eingaben gleich sind. Die Szene hält die PIN nur in einem festen Puffer.

#include <array>
#include <string_view>

#include "ui/scene.hpp"
#include "ui/strings.hpp"

namespace nl::ui {

enum class PinMode : u8 { Verify, Set };

class PinScene final : public Scene {
public:
    void open(PinMode mode);
    // Nächster Frame schließt die Szene; next (optional) öffnet danach, z. B. die Kindersicherung.
    void accept(Scene* next = nullptr) {
        closeNext_ = true;
        next_ = next;
    }
    void reject(Str message);             // Eingabe leeren, Meldung zeigen

    PinMode mode() const { return mode_; }
    std::string_view pinText() const { return {digits_.data(), digits_.size()}; }
    int cursor() const { return cursor_; }
    const char* message() const { return hasMessage_ ? tr(message_) : ""; }

    void update(const UiContext& ctx, const InputFrame& in, float dtMs, SceneRequests& out) override;
    void draw(const UiContext& ctx, gfx::DrawList& dl) override;
    bool isOverlay() const override { return true; }

private:
    void clear();
    gfx::Rect cardRect(const UiContext& ctx) const;

    PinMode mode_ = PinMode::Verify;
    std::array<char, 4> digits_{'0', '0', '0', '0'};
    std::array<char, 4> first_{};  // Set: erste Eingabe
    bool repeat_ = false;          // Set: zweite Eingabe läuft
    int cursor_ = 0;
    Str message_ = Str::PinWrong;
    bool hasMessage_ = false;
    bool closeNext_ = false;
    Scene* next_ = nullptr;
};

}  // namespace nl::ui
