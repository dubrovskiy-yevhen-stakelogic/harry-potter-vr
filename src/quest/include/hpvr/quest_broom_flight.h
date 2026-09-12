#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace hpvr::quest::broom {

using Point = std::array<float, 3>;

struct FlightInput {
    float forward = 0;
    float strafe = 0;
    float vertical = 0;
    Point planar_forward{0, 0, -1};
};

struct FlightConfig {
    float speed = 0;
    float vertical_speed = 0;
    float acceleration = 0;
    float braking = 0;
    float max_delta_seconds = .05F;
};

struct FlightMotion {
    Point velocity{};
};

inline void ResetFlight(FlightMotion& motion) { motion = {}; }

namespace detail {
inline bool Finite(const Point& point) {
    return std::isfinite(point[0]) && std::isfinite(point[1]) && std::isfinite(point[2]);
}

inline bool Nonnegative(float value) { return std::isfinite(value) && value >= 0; }

inline float Axis(float value) {
    return std::isfinite(value) ? std::clamp(value, -1.0F, 1.0F) : 0.0F;
}
} // namespace detail

// Returns world-space displacement in meters; y is up. Collision resolution and
// camera orientation remain the caller's responsibility. Pausing freezes both
// displacement and velocity, while long frames never produce catch-up travel.
[[nodiscard]] inline Point AdvanceFlight(FlightMotion& motion, const FlightInput& input,
                                        const FlightConfig& config, float delta_seconds,
                                        bool paused = false) {
    if (!detail::Finite(motion.velocity) || !detail::Nonnegative(config.speed) ||
        !detail::Nonnegative(config.vertical_speed) || !detail::Nonnegative(config.acceleration) ||
        !detail::Nonnegative(config.braking) || !detail::Nonnegative(config.max_delta_seconds)) {
        ResetFlight(motion);
        return {};
    }
    if (paused || !std::isfinite(delta_seconds) || delta_seconds <= 0) return {};
    const double dt = std::min(delta_seconds, config.max_delta_seconds);
    if (dt <= 0) return {};

    double fx = input.planar_forward[0], fz = input.planar_forward[2];
    const double forward_length = std::hypot(fx, fz);
    if (!std::isfinite(forward_length) || forward_length < 1.0e-6) {
        fx = 0;
        fz = -1;
    } else {
        fx /= forward_length;
        fz /= forward_length;
    }
    double forward = detail::Axis(input.forward), strafe = detail::Axis(input.strafe);
    const double planar_length = std::hypot(forward, strafe);
    if (planar_length > 1) {
        forward /= planar_length;
        strafe /= planar_length;
    }
    const std::array<double, 3> desired{
        (fx * forward - fz * strafe) * config.speed,
        detail::Axis(input.vertical) * double(config.vertical_speed),
        (fz * forward + fx * strafe) * config.speed};
    std::array<double, 3> difference{};
    double difference_squared = 0, current_squared = 0, desired_squared = 0, dot = 0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        difference[axis] = desired[axis] - motion.velocity[axis];
        difference_squared += difference[axis] * difference[axis];
        current_squared += double(motion.velocity[axis]) * motion.velocity[axis];
        desired_squared += desired[axis] * desired[axis];
        dot += double(motion.velocity[axis]) * desired[axis];
    }
    const double rate = (desired_squared < current_squared || dot < 0)
        ? config.braking : config.acceleration;
    const double difference_length = std::sqrt(difference_squared);
    const double ramp_time = rate > 0 ? std::min(dt, difference_length / rate) : 0;
    const double fraction = difference_length > 0 && rate > 0
        ? std::min(1.0, rate * dt / difference_length) : 0;
    Point displacement{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double before = motion.velocity[axis];
        const double after = before + difference[axis] * fraction;
        displacement[axis] = static_cast<float>((before + after) * .5 * ramp_time +
                                                after * (dt - ramp_time));
        motion.velocity[axis] = static_cast<float>(after);
    }
    if (!detail::Finite(displacement) || !detail::Finite(motion.velocity)) {
        ResetFlight(motion);
        return {};
    }
    return displacement;
}

struct Hoop {
    std::int32_t id = 0;
    Point center{};
    Point normal{0, 0, 1};
    float radius = 0;
};

[[nodiscard]] inline bool ValidHoop(const Hoop& hoop) {
    if (!detail::Finite(hoop.center) || !detail::Finite(hoop.normal) ||
        !std::isfinite(hoop.radius) || hoop.radius <= 0) return false;
    double length_squared = 0;
    for (const float component : hoop.normal) length_squared += double(component) * component;
    return length_squared > 1.0e-12;
}

// Center crossing is required even with VR assistance: merely brushing a rim
// with the player's capsule does not count. The optional radial allowance is
// limited to 18 cm and 15% of the authored radius, whichever is smaller.
[[nodiscard]] inline bool SweepHoop(const Point& previous, const Point& current,
                                    const Hoop& hoop, float* fraction = nullptr,
                                    float assist_radius = 0) {
    if (!detail::Finite(previous) || !detail::Finite(current) || !ValidHoop(hoop) ||
        !detail::Nonnegative(assist_radius)) return false;
    double length_squared = 0;
    for (const float component : hoop.normal) length_squared += double(component) * component;
    const double length = std::sqrt(length_squared);
    std::array<double, 3> normal{};
    double before = 0, after = 0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        normal[axis] = hoop.normal[axis] / length;
        before += (double(previous[axis]) - hoop.center[axis]) * normal[axis];
        after += (double(current[axis]) - hoop.center[axis]) * normal[axis];
    }
    if (!((before < 0 && after >= 0) || (before > 0 && after <= 0))) return false;
    const double t = before / (before - after);
    std::array<double, 3> local{};
    double plane_distance = 0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        local[axis] = double(previous[axis]) + (double(current[axis]) - previous[axis]) * t -
                      hoop.center[axis];
        plane_distance += local[axis] * normal[axis];
    }
    double radial_squared = 0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double radial = local[axis] - plane_distance * normal[axis];
        radial_squared += radial * radial;
    }
    const double radius = double(hoop.radius) + std::min({assist_radius, .18F, hoop.radius * .15F});
    if (radial_squared > radius * radius) return false;
    if (fraction) *fraction = static_cast<float>(t);
    return true;
}

enum class RouteStatus { Idle, Active, Completed, TimedOut, Invalid };

struct RouteState {
    std::int32_t stage_id = 0;
    std::size_t route_size = 0;
    std::size_t next_index = 0;
    std::size_t hit_count = 0;
    std::size_t required_count = 0;
    double elapsed_seconds = 0;
    float time_limit_seconds = 0;
    RouteStatus status = RouteStatus::Idle;
};

struct RouteUpdate {
    std::size_t hits_added = 0;
    bool completed = false;
    bool timed_out = false;
};

inline void ResetRoute(RouteState& state) { state = {}; }

// Route data must remain unchanged until the next BeginRoute. Zero required
// count means the whole route; zero time limit means untimed. Stage promotion
// and the authored reward/pass policy are deliberately left to the caller.
[[nodiscard]] inline bool BeginRoute(RouteState& state, std::int32_t stage_id,
                                     std::span<const Hoop> route, float time_limit_seconds,
                                     std::size_t required_count = 0) {
    ResetRoute(state);
    state.status = RouteStatus::Invalid;
    if (route.empty() || !detail::Nonnegative(time_limit_seconds) || required_count > route.size())
        return false;
    for (std::size_t index = 0; index < route.size(); ++index) {
        if (!ValidHoop(route[index])) return false;
        for (std::size_t other = 0; other < index; ++other)
            if (route[other].id == route[index].id) return false;
    }
    state.stage_id = stage_id;
    state.route_size = route.size();
    state.required_count = required_count ? required_count : route.size();
    state.time_limit_seconds = time_limit_seconds;
    state.status = RouteStatus::Active;
    return true;
}

inline void RetryRoute(RouteState& state) {
    if (state.status == RouteStatus::Idle || state.status == RouteStatus::Invalid) return;
    state.next_index = 0;
    state.hit_count = 0;
    state.elapsed_seconds = 0;
    state.status = RouteStatus::Active;
}

// Sweeps only the next authored hoop, then subsequent hoops reached later in
// the same movement segment. It never counts a late hoop crossed out of order.
// Skip this call for teleports/recentering; those are not flight movement.
// Timeout uses elapsed time rather than the motion clamp, excluding pauses.
[[nodiscard]] inline RouteUpdate AdvanceRoute(RouteState& state, std::span<const Hoop> route,
                                              const Point& previous, const Point& current,
                                              float delta_seconds, bool paused = false,
                                              float assist_radius = 0) {
    RouteUpdate update{};
    if (state.status != RouteStatus::Active || paused) return update;
    if (route.size() != state.route_size || state.next_index >= route.size() ||
        state.required_count == 0 || state.required_count > route.size() ||
        !detail::Nonnegative(state.time_limit_seconds) || !detail::Nonnegative(assist_radius) ||
        !std::isfinite(state.elapsed_seconds) || state.elapsed_seconds < 0) {
        state.status = RouteStatus::Invalid;
        return update;
    }
    if (!std::isfinite(delta_seconds) || delta_seconds <= 0) return update;
    const double before_time = state.elapsed_seconds;
    const double remaining = state.time_limit_seconds > 0
        ? std::max(0.0, double(state.time_limit_seconds) - before_time) : delta_seconds;
    const double active_fraction = std::min(1.0, remaining / delta_seconds);
    float last_crossing = -1;
    while (state.next_index < route.size()) {
        float crossing = 0;
        if (!SweepHoop(previous, current, route[state.next_index], &crossing, assist_radius) ||
            crossing <= last_crossing || crossing > active_fraction) break;
        last_crossing = crossing;
        ++state.next_index;
        ++state.hit_count;
        ++update.hits_added;
        if (state.hit_count >= state.required_count) {
            state.elapsed_seconds = before_time + double(delta_seconds) * crossing;
            state.status = RouteStatus::Completed;
            update.completed = true;
            return update;
        }
    }
    state.elapsed_seconds = before_time + std::min(double(delta_seconds), remaining);
    if (state.time_limit_seconds > 0 && state.elapsed_seconds >= state.time_limit_seconds) {
        state.status = RouteStatus::TimedOut;
        update.timed_out = true;
    }
    return update;
}

} // namespace hpvr::quest::broom
