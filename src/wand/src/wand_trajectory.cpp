#include "hpvr/wand_trajectory.h"

#include <cmath>
#include <utility>

namespace hpvr::wand {
namespace {

constexpr float kMaximumAxisDot = 1.0e-3F;

struct PlaneBasis {
    Vec3 origin;
    Vec3 right;
    Vec3 up;
    float width{};
    float height{};
};

struct TimedPoint {
    std::int64_t time_ns{};
    Vec2 point{};
};

struct TimeResampleResult {
    std::vector<Vec2> points;
    std::size_t resampled_away_count{};
    std::size_t interpolated_point_count{};
};

[[nodiscard]] bool is_finite(float value) {
    return std::isfinite(value);
}

[[nodiscard]] bool is_finite(Vec3 value) {
    return is_finite(value.x) && is_finite(value.y) && is_finite(value.z);
}

[[nodiscard]] Vec3 subtract(Vec3 left, Vec3 right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] float dot(Vec3 left, Vec3 right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] std::optional<Vec3> normalized(Vec3 value) {
    const double length = std::hypot(
        static_cast<double>(value.x),
        static_cast<double>(value.y),
        static_cast<double>(value.z));
    if (!std::isfinite(length) || length < 1.0e-4) {
        return std::nullopt;
    }
    return Vec3{
        static_cast<float>(static_cast<double>(value.x) / length),
        static_cast<float>(static_cast<double>(value.y) / length),
        static_cast<float>(static_cast<double>(value.z) / length),
    };
}

[[nodiscard]] std::optional<PlaneBasis> make_basis(const LessonPlane& plane) {
    if (!is_finite(plane.origin_m) || !is_finite(plane.right) ||
        !is_finite(plane.up) || !is_finite(plane.width_m) ||
        !is_finite(plane.height_m) || plane.width_m <= 0.0F ||
        plane.height_m <= 0.0F) {
        return std::nullopt;
    }

    const auto right = normalized(plane.right);
    const auto up = normalized(plane.up);
    if (!right || !up) {
        return std::nullopt;
    }

    if (std::abs(dot(*right, *up)) > kMaximumAxisDot) {
        return std::nullopt;
    }

    return PlaneBasis{plane.origin_m, *right, *up, plane.width_m, plane.height_m};
}

[[nodiscard]] std::optional<Vec2> project_with_basis(
    Vec3 tip_position_m,
    const PlaneBasis& basis) {
    if (!is_finite(tip_position_m)) {
        return std::nullopt;
    }

    const Vec3 offset = subtract(tip_position_m, basis.origin);
    const Vec2 projected{
        0.5F + dot(offset, basis.right) / basis.width,
        0.5F - dot(offset, basis.up) / basis.height,
    };
    if (!is_finite(projected.x) || !is_finite(projected.y)) {
        return std::nullopt;
    }
    return projected;
}

[[nodiscard]] std::uint64_t elapsed_ns(std::int64_t time, std::int64_t start) {
    return static_cast<std::uint64_t>(time) - static_cast<std::uint64_t>(start);
}

[[nodiscard]] TimeResampleResult time_resample(
    const std::vector<TimedPoint>& points,
    std::size_t max_points,
    std::int64_t period_ns) {
    TimeResampleResult result;
    if (points.empty() || max_points == 0) {
        return result;
    }
    if (points.size() == 1 || max_points == 1) {
        result.points.push_back(points.front().point);
        return result;
    }

    const std::int64_t start_time = points.front().time_ns;
    const std::uint64_t duration = elapsed_ns(points.back().time_ns, start_time);
    const std::uint64_t period = static_cast<std::uint64_t>(period_ns);
    const std::uint64_t desired_intervals =
        duration / period + std::uint64_t{duration % period != 0};
    const bool capacity_limited =
        desired_intervals > static_cast<std::uint64_t>(max_points - 1);
    std::size_t output_count = max_points;
    if (desired_intervals < static_cast<std::uint64_t>(max_points - 1)) {
        output_count = static_cast<std::size_t>(desired_intervals) + 1;
    }
    if (output_count < 2) {
        output_count = 2;
    }

    result.points.reserve(output_count);
    const std::uint64_t output_intervals =
        static_cast<std::uint64_t>(output_count - 1);
    std::size_t right_index = 0;
    std::size_t preserved_input_count = 0;
    for (std::size_t index = 0; index < output_count; ++index) {
        const std::uint64_t interval_index = static_cast<std::uint64_t>(index);
        const std::uint64_t target_elapsed = index + 1 == output_count
            ? duration
            : capacity_limited
                ? (duration / output_intervals) * interval_index +
                      ((duration % output_intervals) * interval_index) /
                          output_intervals
                : interval_index * period;

        while (right_index < points.size() &&
               elapsed_ns(points[right_index].time_ns, start_time) < target_elapsed) {
            ++right_index;
        }

        if (right_index < points.size() &&
            elapsed_ns(points[right_index].time_ns, start_time) == target_elapsed) {
            result.points.push_back(points[right_index].point);
            ++preserved_input_count;
            continue;
        }

        const auto& left = points[right_index - 1];
        const auto& right = points[right_index];
        const std::uint64_t segment_start = elapsed_ns(left.time_ns, start_time);
        const std::uint64_t segment_duration = elapsed_ns(right.time_ns, left.time_ns);
        const float blend =
            static_cast<float>(static_cast<long double>(target_elapsed - segment_start) /
                               static_cast<long double>(segment_duration));
        result.points.push_back({
            left.point.x + (right.point.x - left.point.x) * blend,
            left.point.y + (right.point.y - left.point.y) * blend,
        });
        ++result.interpolated_point_count;
    }
    result.resampled_away_count = points.size() - preserved_input_count;
    return result;
}

}  // namespace

std::optional<Vec2> project_tip(Vec3 tip_position_m, const LessonPlane& plane) {
    const auto basis = make_basis(plane);
    if (!basis) {
        return std::nullopt;
    }
    return project_with_basis(tip_position_m, *basis);
}

ProjectedTrajectory project_trajectory(
    std::span<const TrackedTipSample> samples,
    const LessonPlane& plane,
    ProjectionOptions options) {
    ProjectedTrajectory result;
    result.input_sample_count = samples.size();

    const auto basis = make_basis(plane);
    if (!basis) {
        result.status = ProjectionStatus::invalid_plane;
        return result;
    }
    if (!is_finite(options.minimum_tip_distance_m) ||
        options.minimum_tip_distance_m < 0.0F ||
        options.max_points < 2 ||
        options.max_points > kAuthoredLessonPointCapacity ||
        options.resampling_period_ns <= 0) {
        result.status = ProjectionStatus::invalid_options;
        return result;
    }

    std::optional<std::int64_t> last_seen_timestamp;

    std::vector<TimedPoint> raw_projected;
    raw_projected.reserve(samples.size());
    for (const auto& sample : samples) {
        if (last_seen_timestamp &&
            sample.predicted_display_time_ns <= *last_seen_timestamp) {
            ++result.non_monotonic_sample_count;
            continue;
        }
        last_seen_timestamp = sample.predicted_display_time_ns;

        if (!sample.pose_valid || !is_finite(sample.position_m)) {
            ++result.invalid_sample_count;
            continue;
        }

        const auto point = project_with_basis(sample.position_m, *basis);
        if (!point) {
            ++result.invalid_sample_count;
            continue;
        }

        raw_projected.push_back({sample.predicted_display_time_ns, *point});
    }

    std::optional<Vec2> last_accepted_point;
    std::vector<TimedPoint> projected;
    projected.reserve(raw_projected.size());
    for (std::size_t index = 0; index < raw_projected.size(); ++index) {
        const auto& timed_point = raw_projected[index];
        const bool preserve_endpoint =
            index == 0 || index + 1 == raw_projected.size();
        if (!preserve_endpoint && last_accepted_point &&
            options.minimum_tip_distance_m > 0.0F) {
            const double physical_x =
                static_cast<double>(timed_point.point.x - last_accepted_point->x) *
                basis->width;
            const double physical_y =
                static_cast<double>(timed_point.point.y - last_accepted_point->y) *
                basis->height;
            if (std::hypot(physical_x, physical_y) < options.minimum_tip_distance_m) {
                ++result.jitter_rejected_count;
                continue;
            }
        }

        projected.push_back(timed_point);
        last_accepted_point = timed_point.point;
    }

    if (result.non_monotonic_sample_count != 0) {
        result.status = ProjectionStatus::invalid_timing;
        return result;
    }
    if (result.invalid_sample_count != 0) {
        result.status = ProjectionStatus::tracking_lost;
        return result;
    }

    auto resampled =
        time_resample(projected, options.max_points, options.resampling_period_ns);
    result.points = std::move(resampled.points);
    result.resampled_away_count = resampled.resampled_away_count;
    result.interpolated_point_count = resampled.interpolated_point_count;
    return result;
}

}  // namespace hpvr::wand
