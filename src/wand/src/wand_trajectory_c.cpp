#include "hpvr/wand_trajectory_c.h"

#include "hpvr/wand_trajectory.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <vector>

namespace {

using hpvr::wand::LessonPlane;
using hpvr::wand::ProjectionOptions;
using hpvr::wand::ProjectionStatus;
using hpvr::wand::TrackedTipSample;
using hpvr::wand::Vec3;

static_assert(sizeof(hpvr_wand_vec2) == 8);
static_assert(alignof(hpvr_wand_vec2) == 4);
static_assert(std::is_standard_layout_v<hpvr_wand_vec2>);
static_assert(offsetof(hpvr_wand_vec2, x) == 0);
static_assert(offsetof(hpvr_wand_vec2, y) == 4);
static_assert(sizeof(hpvr_wand_tracked_tip_sample) == 24);
static_assert(alignof(hpvr_wand_tracked_tip_sample) == 8);
static_assert(std::is_standard_layout_v<hpvr_wand_tracked_tip_sample>);
static_assert(offsetof(hpvr_wand_tracked_tip_sample, predicted_display_time_ns) == 0);
static_assert(offsetof(hpvr_wand_tracked_tip_sample, position_x_m) == 8);
static_assert(offsetof(hpvr_wand_tracked_tip_sample, position_y_m) == 12);
static_assert(offsetof(hpvr_wand_tracked_tip_sample, position_z_m) == 16);
static_assert(offsetof(hpvr_wand_tracked_tip_sample, pose_valid) == 20);
static_assert(sizeof(hpvr_wand_lesson_plane) == 44);
static_assert(alignof(hpvr_wand_lesson_plane) == 4);
static_assert(std::is_standard_layout_v<hpvr_wand_lesson_plane>);
static_assert(offsetof(hpvr_wand_lesson_plane, origin_x_m) == 0);
static_assert(offsetof(hpvr_wand_lesson_plane, right_x) == 12);
static_assert(offsetof(hpvr_wand_lesson_plane, up_x) == 24);
static_assert(offsetof(hpvr_wand_lesson_plane, width_m) == 36);
static_assert(offsetof(hpvr_wand_lesson_plane, height_m) == 40);
static_assert(sizeof(hpvr_wand_projection_options) == 16);
static_assert(alignof(hpvr_wand_projection_options) == 8);
static_assert(std::is_standard_layout_v<hpvr_wand_projection_options>);
static_assert(offsetof(hpvr_wand_projection_options, max_points) == 0);
static_assert(offsetof(hpvr_wand_projection_options, minimum_tip_distance_m) == 4);
static_assert(offsetof(hpvr_wand_projection_options, resampling_period_ns) == 8);
static_assert(sizeof(hpvr_wand_projection_report) == 32);
static_assert(alignof(hpvr_wand_projection_report) == 4);
static_assert(std::is_standard_layout_v<hpvr_wand_projection_report>);
static_assert(offsetof(hpvr_wand_projection_report, status) == 0);
static_assert(offsetof(hpvr_wand_projection_report, output_point_count) == 4);
static_assert(offsetof(hpvr_wand_projection_report, interpolated_point_count) == 28);

[[nodiscard]] uint32_t status_code(ProjectionStatus status) noexcept {
    switch (status) {
        case ProjectionStatus::ok:
            return HPVR_WAND_PROJECTION_OK;
        case ProjectionStatus::invalid_plane:
            return HPVR_WAND_PROJECTION_INVALID_PLANE;
        case ProjectionStatus::invalid_options:
            return HPVR_WAND_PROJECTION_INVALID_OPTIONS;
        case ProjectionStatus::tracking_lost:
            return HPVR_WAND_PROJECTION_TRACKING_LOST;
        case ProjectionStatus::invalid_timing:
            return HPVR_WAND_PROJECTION_INVALID_TIMING;
    }
    return HPVR_WAND_PROJECTION_INTERNAL_ERROR;
}

void initialize_report(
    hpvr_wand_projection_report* report,
    uint32_t status) noexcept {
    if (report != nullptr) {
        *report = {};
        report->status = status;
    }
}

}  // namespace

uint32_t hpvr_wand_abi_version(void) noexcept {
    return HPVR_WAND_ABI_VERSION;
}

uint32_t hpvr_wand_project_trajectory(
    const hpvr_wand_tracked_tip_sample* samples,
    uint32_t sample_count,
    const hpvr_wand_lesson_plane* plane,
    const hpvr_wand_projection_options* options,
    hpvr_wand_vec2* output_points,
    uint32_t output_capacity,
    hpvr_wand_projection_report* output_report) noexcept {
    if (output_report == nullptr || plane == nullptr || options == nullptr ||
        (sample_count != 0 && samples == nullptr) ||
        (output_capacity != 0 && output_points == nullptr)) {
        initialize_report(output_report, HPVR_WAND_PROJECTION_INVALID_ARGUMENT);
        return HPVR_WAND_PROJECTION_INVALID_ARGUMENT;
    }

    initialize_report(output_report, HPVR_WAND_PROJECTION_INTERNAL_ERROR);
    try {
        std::vector<TrackedTipSample> input;
        input.reserve(sample_count);
        for (uint32_t index = 0; index < sample_count; ++index) {
            const auto& sample = samples[index];
            input.push_back({
                sample.predicted_display_time_ns,
                Vec3{
                    sample.position_x_m,
                    sample.position_y_m,
                    sample.position_z_m,
                },
                sample.pose_valid != 0,
            });
        }

        const LessonPlane native_plane{
            .origin_m = {plane->origin_x_m, plane->origin_y_m, plane->origin_z_m},
            .right = {plane->right_x, plane->right_y, plane->right_z},
            .up = {plane->up_x, plane->up_y, plane->up_z},
            .width_m = plane->width_m,
            .height_m = plane->height_m,
        };
        const ProjectionOptions native_options{
            .max_points = options->max_points,
            .minimum_tip_distance_m = options->minimum_tip_distance_m,
            .resampling_period_ns = options->resampling_period_ns,
        };
        const auto result =
            hpvr::wand::project_trajectory(input, native_plane, native_options);

        const uint32_t projected_status = status_code(result.status);
        output_report->status = projected_status;
        output_report->output_point_count =
            static_cast<uint32_t>(result.points.size());
        output_report->input_sample_count =
            static_cast<uint32_t>(result.input_sample_count);
        output_report->invalid_sample_count =
            static_cast<uint32_t>(result.invalid_sample_count);
        output_report->non_monotonic_sample_count =
            static_cast<uint32_t>(result.non_monotonic_sample_count);
        output_report->jitter_rejected_count =
            static_cast<uint32_t>(result.jitter_rejected_count);
        output_report->resampled_away_count =
            static_cast<uint32_t>(result.resampled_away_count);
        output_report->interpolated_point_count =
            static_cast<uint32_t>(result.interpolated_point_count);

        if (projected_status != HPVR_WAND_PROJECTION_OK) {
            return projected_status;
        }
        if (result.points.size() > output_capacity) {
            output_report->status = HPVR_WAND_PROJECTION_BUFFER_TOO_SMALL;
            return HPVR_WAND_PROJECTION_BUFFER_TOO_SMALL;
        }
        if (!result.points.empty() && output_points == nullptr) {
            output_report->status = HPVR_WAND_PROJECTION_INVALID_ARGUMENT;
            return HPVR_WAND_PROJECTION_INVALID_ARGUMENT;
        }
        std::transform(
            result.points.begin(),
            result.points.end(),
            output_points,
            [](const hpvr::wand::Vec2 point) {
                return hpvr_wand_vec2{point.x, point.y};
            });
        return HPVR_WAND_PROJECTION_OK;
    } catch (const std::bad_alloc&) {
        initialize_report(output_report, HPVR_WAND_PROJECTION_ALLOCATION_FAILURE);
        return HPVR_WAND_PROJECTION_ALLOCATION_FAILURE;
    } catch (...) {
        initialize_report(output_report, HPVR_WAND_PROJECTION_INTERNAL_ERROR);
        return HPVR_WAND_PROJECTION_INTERNAL_ERROR;
    }
}
