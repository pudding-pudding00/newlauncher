#pragma once
// Animationen (Spec 3.1/3.6): 200 ms ease-out als Standard.

namespace nl {

inline float easeOutCubic(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}

// Wert, der in einer festen Dauer mit ease-out auf ein Ziel zuläuft.
class Tween {
public:
    void snap(float v) {
        from_ = to_ = v;
        elapsed_ = duration_ = 0.0f;
    }
    void to(float target, float durationMs) {
        from_ = value();
        to_ = target;
        elapsed_ = 0.0f;
        duration_ = durationMs > 0.0f ? durationMs : 0.0f;
    }
    void update(float dtMs) {
        if (dtMs > 0.0f) elapsed_ += dtMs;
        if (elapsed_ > duration_) elapsed_ = duration_;
    }
    float value() const {
        if (done()) return to_;
        return from_ + (to_ - from_) * easeOutCubic(elapsed_ / duration_);
    }
    float target() const { return to_; }
    bool done() const { return elapsed_ >= duration_; }

private:
    float from_ = 0.0f, to_ = 0.0f, elapsed_ = 0.0f, duration_ = 0.0f;
};

}  // namespace nl
