#pragma once

#include "hpvr/quest_broom_flight.h"

#include <limits>

namespace hpvr::quest::broom {

struct SessionConfig {
    std::array<std::span<const Hoop>, 5> stages;
    std::array<float, 5> stage_time_bonuses{};
    std::size_t minimum_hits = 0;
    float hoop_assist_radius = 0;
};

enum class SessionGrade { None, Redo, Pass, Good, Excellent, Perfect, Invalid };
enum class SessionStatus { Idle, Active, Finished, Invalid };

struct SessionResult {
    SessionGrade grade = SessionGrade::None;
    unsigned house_points = 0;
    bool passed = false;
    bool alternate_path = false;
};

[[nodiscard]] inline SessionResult EvaluateSession(std::size_t hits, std::size_t total,
                                                   std::size_t minimum_hits) {
    if (total == 0 || minimum_hits == 0 || minimum_hits > total || hits > total)
        return {SessionGrade::Invalid};
    if (hits == total) return {SessionGrade::Perfect, 20, true, true};
    const auto spread = total - minimum_hits;
    const auto excellent = minimum_hits + (spread / 3) * 2 + (spread % 3) * 2 / 3;
    const auto good = minimum_hits + spread / 3;
    if (hits >= excellent) return {SessionGrade::Excellent, 15, true, false};
    if (hits >= good) return {SessionGrade::Good, 10, true, false};
    if (hits >= minimum_hits) return {SessionGrade::Pass, 5, true, false};
    return {SessionGrade::Redo, 0, false, false};
}

[[nodiscard]] inline const char* SessionResultTag(SessionGrade grade) {
    switch (grade) {
    case SessionGrade::Redo: return "redo";
    case SessionGrade::Pass: return "pass";
    case SessionGrade::Good: return "good";
    case SessionGrade::Excellent: return "excellent";
    case SessionGrade::Perfect: return "perfect";
    default: return "";
    }
}

struct SessionState {
    RouteState route;
    std::size_t stage = 0;
    std::size_t hits = 0;
    std::size_t total_hoops = 0;
    SessionStatus status = SessionStatus::Idle;
    SessionResult result;
    Point previous{};
    bool previous_valid = false;
};

struct SessionUpdate {
    std::size_t hits_added = 0;
    std::size_t stages_advanced = 0;
    bool finished = false;
};

inline void ResetSession(SessionState& state) { state = {}; }

// Call after a teleport/recenter and before subsequent resolved flight motion.
inline void RebaseSession(SessionState& state, const Point& position) {
    state.previous_valid = detail::Finite(position);
    state.previous = state.previous_valid ? position : Point{};
}

[[nodiscard]] inline double SessionTimeRemaining(const SessionState& state) {
    if (state.status == SessionStatus::Idle || state.status == SessionStatus::Invalid) return 0;
    return std::max(0.0, double(state.route.time_limit_seconds) - state.route.elapsed_seconds);
}

// All geometry is already in world meters. Config owns no data: keep stage
// spans alive and their identities/order fixed throughout a trial. Authored
// animated centers may change between samples. No cinematics or path choice
// are implied by starting or finishing a session.
[[nodiscard]] inline bool BeginSession(SessionState& state, const SessionConfig& config) {
    ResetSession(state);
    state.status = SessionStatus::Invalid;
    if (!detail::Nonnegative(config.hoop_assist_radius)) return false;
    double maximum_time = 0;
    for (std::size_t stage = 0; stage < config.stages.size(); ++stage) {
        const auto route = config.stages[stage];
        RouteState checked;
        if (!BeginRoute(checked, static_cast<std::int32_t>(stage + 1), route, 0) ||
            !detail::Nonnegative(config.stage_time_bonuses[stage]) ||
            (stage == 0 && config.stage_time_bonuses[stage] == 0) ||
            route.size() > std::numeric_limits<std::size_t>::max() - state.total_hoops)
            return false;
        state.total_hoops += route.size();
        maximum_time += config.stage_time_bonuses[stage];
        for (const auto& hoop : route)
            for (std::size_t previous_stage = 0; previous_stage < stage; ++previous_stage)
                for (const auto& previous : config.stages[previous_stage])
                    if (previous.id == hoop.id) return false;
    }
    if (maximum_time > std::numeric_limits<float>::max() ||
        config.minimum_hits == 0 || config.minimum_hits > state.total_hoops) return false;
    if (!BeginRoute(state.route, 1, config.stages[0], config.stage_time_bonuses[0])) return false;
    state.status = SessionStatus::Active;
    return true;
}

[[nodiscard]] inline bool RetrySession(SessionState& state, const SessionConfig& config) {
    return BeginSession(state, config);
}

// Feed collision-resolved positions only. The first sample establishes a
// baseline; a paused or discontinuous sample cannot sweep through hoop routes.
// Later stage times are bonuses added to the countdown still left, not new
// independent time limits. Minimum hits affects assessment only, never an
// early end to the trial.
[[nodiscard]] inline SessionUpdate AdvanceSession(SessionState& state, const SessionConfig& config,
                                                  const Point& current, float delta_seconds,
                                                  bool paused = false, bool discontinuity = false) {
    SessionUpdate update{};
    if (state.status != SessionStatus::Active) return update;
    if (paused) {
        state.previous_valid = false;
        return update;
    }
    if (!std::isfinite(delta_seconds) || delta_seconds <= 0) {
        state.previous_valid = false;
        return update;
    }
    if (state.stage >= config.stages.size() || config.minimum_hits == 0 ||
        config.minimum_hits > state.total_hoops || !detail::Nonnegative(config.hoop_assist_radius)) {
        state.status = SessionStatus::Invalid;
        return update;
    }
    const bool can_sweep = state.previous_valid && !discontinuity && detail::Finite(current);
    Point from = can_sweep ? state.previous : (detail::Finite(current) ? current : Point{});
    const Point to = detail::Finite(current) ? current : Point{};
    RebaseSession(state, current);
    float seconds = delta_seconds;
    const auto finish = [&]() {
        state.result = EvaluateSession(state.hits, state.total_hoops, config.minimum_hits);
        state.status = state.result.grade == SessionGrade::Invalid
            ? SessionStatus::Invalid : SessionStatus::Finished;
        update.finished = state.status == SessionStatus::Finished;
    };
    // At most five stage transitions can occur during a single movement.
    while (seconds > 0 && state.stage < config.stages.size()) {
        const double elapsed_before = state.route.elapsed_seconds;
        const auto step = AdvanceRoute(state.route, config.stages[state.stage], from, to, seconds,
                                       false, config.hoop_assist_radius);
        state.hits += step.hits_added;
        update.hits_added += step.hits_added;
        if (state.route.status == RouteStatus::Invalid) {
            state.status = SessionStatus::Invalid;
            return update;
        }
        if (step.timed_out) {
            finish();
            return update;
        }
        if (!step.completed) return update;
        if (state.stage + 1 == config.stages.size()) {
            finish();
            return update;
        }
        const double consumed = std::clamp(state.route.elapsed_seconds - elapsed_before,
                                           0.0, double(seconds));
        const double remaining = SessionTimeRemaining(state);
        const auto next_stage = state.stage + 1;
        const float bonus = config.stage_time_bonuses[next_stage];
        if (!detail::Nonnegative(bonus) || remaining + bonus > std::numeric_limits<float>::max()) {
            state.status = SessionStatus::Invalid;
            return update;
        }
        const double next_limit = remaining + bonus;
        if (next_limit <= 0) {
            finish();
            return update;
        }
        const double fraction = consumed / seconds;
        for (std::size_t axis = 0; axis < 3; ++axis)
            from[axis] = static_cast<float>(double(from[axis]) + (double(to[axis]) - from[axis]) * fraction);
        seconds = static_cast<float>(std::max(0.0, double(seconds) - consumed));
        state.stage = next_stage;
        ++update.stages_advanced;
        if (!BeginRoute(state.route, static_cast<std::int32_t>(state.stage + 1),
                        config.stages[state.stage], static_cast<float>(next_limit))) {
            state.status = SessionStatus::Invalid;
            return update;
        }
    }
    return update;
}

} // namespace hpvr::quest::broom
