#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace hpvr::wand {

inline constexpr std::size_t kAuthoredLessonPointCapacity{500};

struct Vec2 {
    float x{};
    float y{};

    friend bool operator==(const Vec2&, const Vec2&) = default;
};

struct Vec3 {
    float x{};
    float y{};
    float z{};

    friend bool operator==(const Vec3&, const Vec3&) = default;
};

// A tip position sampled at an OpenXR predicted display time. The XR host owns
// grip/aim-to-tip calibration; this layer never guesses a controller offset.
// All positions in one trajectory must use the same stable XR reference space.
struct TrackedTipSample {
    std::int64_t predicted_display_time_ns{};
    Vec3 position_m{};
    bool pose_valid{};
};

// Width and height are the full physical dimensions which map to the shipped
// lesson's normalized 0..1 coordinate range. Freeze this plane when recording
// begins and express it in the samples' stable XR reference space. Axes must be
// finite, non-zero, and perpendicular. Values outside the range are not clamped.
struct LessonPlane {
    Vec3 origin_m{};
    Vec3 right{};
    Vec3 up{};
    float width_m{};
    float height_m{};
};

struct ProjectionOptions {
    // A trajectory needs room for distinct start and end points.
    std::size_t max_points{kAuthoredLessonPointCapacity};
    // Physical distance inside the lesson plane. Normal-only motion is ignored.
    float minimum_tip_distance_m{};
    // Required canonical time step. The game host derives this from the
    // lesson's authored DrawTime and point capacity; this layer never guesses it.
    std::int64_t resampling_period_ns{};
};

enum class ProjectionStatus {
    ok,
    invalid_plane,
    invalid_options,
    tracking_lost,
    invalid_timing,
};

struct ProjectedTrajectory {
    ProjectionStatus status{ProjectionStatus::ok};
    std::vector<Vec2> points;
    std::size_t input_sample_count{};
    std::size_t invalid_sample_count{};
    std::size_t non_monotonic_sample_count{};
    std::size_t jitter_rejected_count{};
    std::size_t resampled_away_count{};
    std::size_t interpolated_point_count{};
};

[[nodiscard]] std::optional<Vec2> project_tip(
    Vec3 tip_position_m,
    const LessonPlane& plane);

[[nodiscard]] ProjectedTrajectory project_trajectory(
    std::span<const TrackedTipSample> samples,
    const LessonPlane& plane,
    ProjectionOptions options);

}  // namespace hpvr::wand
