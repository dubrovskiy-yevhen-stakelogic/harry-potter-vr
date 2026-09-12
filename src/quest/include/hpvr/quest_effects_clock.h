#pragma once

#include <algorithm>
#include <cmath>

namespace hpvr::quest {

// World effects must advance independently of a map's character-animation path.
// Accumulate in double so a long session does not change the effective rate;
// bound render time to keep the existing float particle phases precise.
class WorldEffectsClock {
public:
    static constexpr double kWrapSeconds = 4096.0;
    static constexpr float kMaximumStepSeconds = 0.05F;

    void Reset() { elapsed_seconds_ = 0.0; }

    void Advance(float delta_seconds, bool running) {
        if (!running || !std::isfinite(delta_seconds) || delta_seconds <= 0.0F)
            return;
        elapsed_seconds_ += static_cast<double>(
            std::min(delta_seconds, kMaximumStepSeconds));
        if (elapsed_seconds_ >= kWrapSeconds)
            elapsed_seconds_ -= kWrapSeconds;
    }

    float Seconds() const { return static_cast<float>(elapsed_seconds_); }

private:
    double elapsed_seconds_ = 0.0;
};

}  // namespace hpvr::quest
