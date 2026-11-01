#pragma once

#include <cmath>

namespace rb {

// Fixed output transfer. There is no volume setting, level detector, time
// envelope, lookahead, normalization or other persistent state in this stage.
// Below-knee samples retain the same fixed gain for the entire performance;
// only peaks above the knee are softly rounded. This calibration is shared
// by live playback and export, and never changes Android's media volume.
class OutputStage {
public:
    // v30.2: +4.86 dB in the linear region. The wider, gentler knee preserves
    // most of that loudness increase while reducing near-flat peaks and
    // measured codec overshoot versus the former 0.80 knee at this gain.
    // See docs/LOUDNESS_V30_2_VALIDATION.txt for calibration and limits.
    static constexpr float kMakeupGain = 1.75f;
    static constexpr float kKnee = 0.40f;
    static constexpr float kCeiling = 1.0f;

    static float processSample(float input) {
        const float scaled = input * kMakeupGain;
        if (std::isnan(scaled)) return 0.0f;
        const float magnitude = std::fabs(scaled);
        if (magnitude <= kKnee) return scaled;

        // Value and slope both meet the linear region at the knee. The curve
        // approaches a finite ceiling without hard clipping, attack/release
        // behavior or a gain change imposed on later samples.
        constexpr float span = kCeiling - kKnee;
        const float shaped = kKnee + span * std::tanh((magnitude - kKnee) / span);
        return std::copysign(shaped, scaled);
    }

    static void process(float& left, float& right) {
        left = processSample(left);
        right = processSample(right);
    }
};

} // namespace rb
