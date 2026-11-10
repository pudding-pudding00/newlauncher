#include "core/synth.hpp"

#include <cmath>

namespace nl {

std::vector<s16> synthTone(const ToneSpec& spec, u32 sampleRate) {
    const std::size_t n = static_cast<std::size_t>(sampleRate) * spec.ms / 1000;
    std::vector<s16> out((n + 15) / 16 * 16, 0);
    if (n < 2) return out;

    const std::size_t attack = static_cast<std::size_t>(sampleRate) * 5 / 1000;
    const double rate = static_cast<double>(sampleRate);
    const double peak = static_cast<double>(spec.amplitude) * 32767.0;
    double phase = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(n - 1);  // 0..1 über den Ton
        double env;
        if (i < attack) env = static_cast<double>(i) / static_cast<double>(attack);
        else env = static_cast<double>(n - 1 - i) / static_cast<double>(n - 1 - (attack < n - 1 ? attack : 0));
        out[i] = static_cast<s16>(std::lround(std::sin(phase) * env * peak));
        const double hz = spec.startHz + (spec.endHz - spec.startHz) * t;
        phase += 2.0 * 3.14159265358979323846 * hz / rate;
    }
    return out;
}

}  // namespace nl
