#pragma once
#include <algorithm>
#include <cmath>

namespace hpvr::quest::death {
inline float Duration(float faint_seconds) {
    return (std::isfinite(faint_seconds) && faint_seconds > 0 ?
        std::min(faint_seconds, 30.0F) : 0.0F) + .5F;
}
inline float Advance(float elapsed, float seconds, float duration) {
    if (!std::isfinite(seconds) || seconds <= 0) return elapsed;
    return std::min(duration, elapsed + std::min(seconds, .05F));
}
inline float FaintTime(float elapsed, float duration) {
    return std::clamp(elapsed, 0.0F, std::max(0.0F, duration - .5F));
}
}
