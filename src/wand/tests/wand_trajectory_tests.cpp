#include "hpvr/wand_trajectory.h"
#include "hpvr/wand_trajectory_c.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {

using hpvr::wand::LessonPlane;
using hpvr::wand::ProjectionOptions;
using hpvr::wand::ProjectionStatus;
using hpvr::wand::TrackedTipSample;
using hpvr::wand::Vec2;
using hpvr::wand::Vec3;

[[noreturn]] void fail(std::string_view message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, std::string_view message) {
    if (!condition) {
        fail(message);
    }
}

void expect_near(float actual, float expected, std::string_view message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > 1.0e-5F) {
        std::cerr << "FAIL: " << message << " (actual=" << actual
                  << ", expected=" << expected << ")\n";
        std::exit(EXIT_FAILURE);
    }
}

LessonPlane test_plane() {
    return {
        .origin_m = {10.0F, 20.0F, 30.0F},
        .right = {2.0F, 0.0F, 0.0F},
        .up = {0.0F, 3.0F, 0.0F},
        .width_m = 2.0F,
        .height_m = 4.0F,
    };
}

void projection_preserves_hp_lesson_orientation() {
    const auto plane = test_plane();
    const auto center = hpvr::wand::project_tip(plane.origin_m, plane);
    expect(center.has_value(), "center projects");
    expect_near(center->x, 0.5F, "center x");
    expect_near(center->y, 0.5F, "center y");

    const auto right_edge = hpvr::wand::project_tip({11.0F, 20.0F, 30.0F}, plane);
    expect(right_edge.has_value(), "right edge projects");
    expect_near(right_edge->x, 1.0F, "right edge x");
    expect_near(right_edge->y, 0.5F, "right edge y");

    const auto top_edge = hpvr::wand::project_tip({10.0F, 22.0F, 30.0F}, plane);
    expect(top_edge.has_value(), "top edge projects");
    expect_near(top_edge->x, 0.5F, "top edge x");
    expect_near(top_edge->y, 0.0F, "HP lesson y points down");

    const auto outside = hpvr::wand::project_tip({12.0F, 20.0F, 30.0F}, plane);
    expect(outside.has_value(), "outside point projects");
    expect_near(outside->x, 1.5F, "outside point is not clamped");
}

void invalid_planes_are_rejected() {
    auto plane = test_plane();
    plane.up = plane.right;
    expect(!hpvr::wand::project_tip(plane.origin_m, plane).has_value(),
           "collinear plane axes are rejected");

    plane = test_plane();
    plane.width_m = 0.0F;
    expect(!hpvr::wand::project_tip(plane.origin_m, plane).has_value(),
           "zero plane width is rejected");
}

void trajectory_reports_tracking_and_timing_rejections() {
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const std::vector<TrackedTipSample> samples{
        {100, {10.0F, 20.0F, 30.0F}, true},
        {200, {10.0001F, 20.0F, 30.0F}, true},
        {200, {10.5F, 20.0F, 30.0F}, true},
        {300, {nan, 20.0F, 30.0F}, true},
        {400, {11.0F, 20.0F, 30.0F}, true},
        {500, {12.0F, 20.0F, 30.0F}, false},
    };

    const auto result = hpvr::wand::project_trajectory(
        samples,
        test_plane(),
        {
            .max_points = 500,
            .minimum_tip_distance_m = 0.001F,
            .resampling_period_ns = 300,
        });
    expect(result.status == ProjectionStatus::invalid_timing,
           "timing discontinuity cancels the trajectory");
    expect(result.input_sample_count == 6, "input count is reported");
    expect(result.points.empty(), "discontinuous input cannot synthesize a gesture");
    expect(result.jitter_rejected_count == 1, "jitter rejection is reported");
    expect(result.non_monotonic_sample_count == 1,
           "non-monotonic timestamp is reported");
    expect(result.invalid_sample_count == 2, "tracking/NaN rejection is reported");
    expect(result.resampled_away_count == 0, "short trajectory is not resampled");
}

void authored_capacity_preserves_endpoints_and_order() {
    std::vector<TrackedTipSample> samples;
    samples.reserve(1000);
    for (std::int64_t index = 0; index < 1000; ++index) {
        samples.push_back({index + 1, {static_cast<float>(index), 0.0F, 0.0F}, true});
    }

    const LessonPlane plane{
        .origin_m = {0.0F, 0.0F, 0.0F},
        .right = {1.0F, 0.0F, 0.0F},
        .up = {0.0F, 1.0F, 0.0F},
        .width_m = 1000.0F,
        .height_m = 1.0F,
    };
    const auto result = hpvr::wand::project_trajectory(
        samples,
        plane,
        {
            .max_points = 500,
            .minimum_tip_distance_m = 0.0F,
            .resampling_period_ns = 2,
        });
    expect(result.status == ProjectionStatus::ok, "capacity trajectory status");
    expect(result.points.size() == 500, "authored 500-point capacity is enforced");
    expect(result.resampled_away_count == 500, "resampling count is reported");
    expect_near(result.points.front().x, 0.5F, "first point is preserved");
    expect_near(result.points.back().x, 1.499F, "last point is preserved");
    for (std::size_t index = 1; index < result.points.size(); ++index) {
        expect(result.points[index].x > result.points[index - 1].x,
               "resampling preserves point order");
    }
}

void time_resampling_is_cadence_independent() {
    const LessonPlane plane{
        .origin_m = {0.0F, 0.0F, 0.0F},
        .right = {1.0F, 0.0F, 0.0F},
        .up = {0.0F, 1.0F, 0.0F},
        .width_m = 10.0F,
        .height_m = 1.0F,
    };
    const std::vector<TrackedTipSample> irregular{
        {0, {0.0F, 0.0F, 0.0F}, true},
        {1, {1.0F, 0.0F, 0.0F}, true},
        {5, {5.0F, 0.0F, 0.0F}, true},
        {6, {6.0F, 0.0F, 0.0F}, true},
    };

    std::vector<TrackedTipSample> regular;
    for (std::int64_t time = 0; time <= 6; ++time) {
        regular.push_back({time, {static_cast<float>(time), 0.0F, 0.0F}, true});
    }

    const ProjectionOptions options{
        .max_points = 500,
        .minimum_tip_distance_m = 0.0F,
        .resampling_period_ns = 5,
    };
    const auto irregular_result = hpvr::wand::project_trajectory(irregular, plane, options);
    const auto regular_result = hpvr::wand::project_trajectory(regular, plane, options);
    expect(irregular_result.points.size() == 3, "irregular trajectory is resampled");
    expect(regular_result.points.size() == 3, "regular trajectory is resampled");
    expect(irregular_result.resampled_away_count == 1,
           "irregular off-grid source point is reported");
    expect(irregular_result.interpolated_point_count == 0,
           "existing canonical source points need no interpolation");
    expect(regular_result.resampled_away_count == 4,
           "regular off-grid source points are reported");
    expect(regular_result.interpolated_point_count == 0,
           "regular canonical source points need no interpolation");
    for (std::size_t index = 0; index < 3; ++index) {
        expect_near(irregular_result.points[index].x, regular_result.points[index].x,
                    "cadence-independent resampled x");
    }
    expect_near(irregular_result.points[1].x, 1.0F, "middle point uses middle time");
}

void jitter_is_measured_inside_the_lesson_plane() {
    const std::vector<TrackedTipSample> samples{
        {100, {10.0F, 20.0F, 30.0F}, true},
        {200, {10.0F, 20.0F, 31.0F}, true},
        {300, {11.0F, 20.0F, 30.0F}, true},
    };
    const auto result = hpvr::wand::project_trajectory(
        samples,
        test_plane(),
        {
            .max_points = 500,
            .minimum_tip_distance_m = 0.001F,
            .resampling_period_ns = 100,
        });
    expect(result.points.size() == 3, "canonical time grid is retained");
    expect(result.jitter_rejected_count == 1,
           "internal normal-only movement is reported as jitter");
    expect(result.interpolated_point_count == 1,
           "filtered internal point is replaced on the canonical time grid");
}

void resampling_telemetry_counts_replaced_points() {
    const std::vector<TrackedTipSample> samples{
        {0, {10.0F, 20.0F, 30.0F}, true},
        {3, {10.3F, 20.0F, 30.0F}, true},
        {6, {10.6F, 20.0F, 30.0F}, true},
    };
    const auto result = hpvr::wand::project_trajectory(
        samples,
        test_plane(),
        {
            .max_points = 500,
            .minimum_tip_distance_m = 0.0F,
            .resampling_period_ns = 5,
        });
    expect(result.points.size() == 3, "replacement keeps the output point count");
    expect(result.resampled_away_count == 1, "off-grid source point is counted");
    expect(result.interpolated_point_count == 1, "new canonical point is counted");
}

void invalid_pose_timestamps_still_guard_stream_order() {
    const std::vector<TrackedTipSample> samples{
        {100, {10.0F, 20.0F, 30.0F}, true},
        {300, {10.5F, 20.0F, 30.0F}, false},
        {200, {11.0F, 20.0F, 30.0F}, true},
    };
    const auto result = hpvr::wand::project_trajectory(
        samples,
        test_plane(),
        {
            .max_points = 500,
            .minimum_tip_distance_m = 0.0F,
            .resampling_period_ns = 100,
        });
    expect(result.status == ProjectionStatus::invalid_timing,
           "time reversal has explicit failure status");
    expect(result.points.empty(), "time reversal after tracking loss is rejected");
    expect(result.invalid_sample_count == 1, "invalid pose is reported");
    expect(result.non_monotonic_sample_count == 1, "whole-stream time reversal is reported");
}

void tracking_loss_cancels_instead_of_bridging() {
    const std::vector<TrackedTipSample> samples{
        {100, {10.0F, 20.0F, 30.0F}, true},
        {200, {10.5F, 20.0F, 30.0F}, false},
        {300, {11.0F, 20.0F, 30.0F}, true},
    };
    const auto result = hpvr::wand::project_trajectory(
        samples,
        test_plane(),
        {
            .max_points = 500,
            .minimum_tip_distance_m = 0.0F,
            .resampling_period_ns = 50,
        });
    expect(result.status == ProjectionStatus::tracking_lost,
           "tracking loss has explicit failure status");
    expect(result.points.empty(), "tracking loss cannot synthesize a bridge");
    expect(result.invalid_sample_count == 1, "tracking loss is reported");
}

void extreme_finite_axes_normalize_stably() {
    const float maximum = std::numeric_limits<float>::max();
    const LessonPlane plane{
        .origin_m = {0.0F, 0.0F, 0.0F},
        .right = {maximum, 0.0F, 0.0F},
        .up = {0.0F, maximum, 0.0F},
        .width_m = 2.0F,
        .height_m = 2.0F,
    };
    const auto point = hpvr::wand::project_tip({1.0F, 0.0F, 0.0F}, plane);
    expect(point.has_value(), "extreme finite axes remain valid");
    expect_near(point->x, 1.0F, "extreme right axis normalizes");
    expect_near(point->y, 0.5F, "extreme up axis normalizes");
}

void large_absolute_timestamps_keep_relative_precision() {
    const std::int64_t end = std::numeric_limits<std::int64_t>::max();
    const std::vector<TrackedTipSample> samples{
        {end - 10, {0.0F, 0.0F, 0.0F}, true},
        {end - 9, {1.0F, 0.0F, 0.0F}, true},
        {end, {10.0F, 0.0F, 0.0F}, true},
    };
    const LessonPlane plane{
        .origin_m = {0.0F, 0.0F, 0.0F},
        .right = {1.0F, 0.0F, 0.0F},
        .up = {0.0F, 1.0F, 0.0F},
        .width_m = 10.0F,
        .height_m = 1.0F,
    };
    const auto result = hpvr::wand::project_trajectory(
        samples,
        plane,
        {
            .max_points = 500,
            .minimum_tip_distance_m = 0.0F,
            .resampling_period_ns = 5,
        });
    expect(result.points.size() == 3, "large timestamps produce the canonical grid");
    expect_near(result.points[1].x, 1.0F, "relative timestamp interpolation stays precise");
}

void invalid_options_are_rejected() {
    const std::vector<TrackedTipSample> samples{
        {1, {10.0F, 20.0F, 30.0F}, true},
    };
    const auto result = hpvr::wand::project_trajectory(
        samples,
        test_plane(),
        {
            .max_points = 500,
            .minimum_tip_distance_m = -1.0F,
            .resampling_period_ns = 1,
        });
    expect(result.status == ProjectionStatus::invalid_options,
           "negative jitter distance is rejected");

    const auto missing_period = hpvr::wand::project_trajectory(
        samples,
        test_plane(),
        {
            .max_points = 500,
            .minimum_tip_distance_m = 0.0F,
            .resampling_period_ns = 0,
        });
    expect(missing_period.status == ProjectionStatus::invalid_options,
           "missing canonical period is rejected");

    const auto insufficient_capacity = hpvr::wand::project_trajectory(
        samples,
        test_plane(),
        {
            .max_points = 1,
            .minimum_tip_distance_m = 0.0F,
            .resampling_period_ns = 1,
        });
    expect(insufficient_capacity.status == ProjectionStatus::invalid_options,
           "capacity without distinct endpoints is rejected");

    const auto excessive_capacity = hpvr::wand::project_trajectory(
        samples,
        test_plane(),
        {
            .max_points = hpvr::wand::kAuthoredLessonPointCapacity + 1,
            .minimum_tip_distance_m = 0.0F,
            .resampling_period_ns = 1,
        });
    expect(excessive_capacity.status == ProjectionStatus::invalid_options,
           "capacity above HP1's authored storage is rejected");
}

void c_abi_projects_without_cpp_containers_crossing_boundary() {
    expect(hpvr_wand_abi_version() == HPVR_WAND_ABI_VERSION,
           "C ABI version matches the header");
    const std::array<hpvr_wand_tracked_tip_sample, 3> samples{{
        {0, 0.0F, 0.0F, 0.0F, 1, {}},
        {50, 0.5F, 0.0F, 0.0F, 1, {}},
        {100, 1.0F, 0.0F, 0.0F, 1, {}},
    }};
    const hpvr_wand_lesson_plane plane{
        0.0F, 0.0F, 0.0F,
        1.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F,
        2.0F, 2.0F,
    };
    const hpvr_wand_projection_options options{500, 0.0F, 50};
    std::array<hpvr_wand_vec2, HPVR_WAND_AUTHORED_POINT_CAPACITY> output{};
    hpvr_wand_projection_report report{};
    const auto status = hpvr_wand_project_trajectory(
        samples.data(),
        static_cast<uint32_t>(samples.size()),
        &plane,
        &options,
        output.data(),
        static_cast<uint32_t>(output.size()),
        &report);

    expect(status == HPVR_WAND_PROJECTION_OK, "C ABI projection succeeds");
    expect(report.status == status, "C ABI status is mirrored in the report");
    expect(report.input_sample_count == 3, "C ABI reports all input samples");
    expect(report.output_point_count == 3, "C ABI reports projected points");
    expect_near(output.front().x, 0.5F, "C ABI preserves first x");
    expect_near(output[2].x, 1.0F, "C ABI preserves last x");
    expect_near(output.front().y, 0.5F, "C ABI preserves HP y orientation");
}

void c_abi_reports_required_capacity_without_partial_output() {
    const std::array<hpvr_wand_tracked_tip_sample, 3> samples{{
        {0, 0.0F, 0.0F, 0.0F, 1, {}},
        {50, 0.5F, 0.0F, 0.0F, 1, {}},
        {100, 1.0F, 0.0F, 0.0F, 1, {}},
    }};
    const hpvr_wand_lesson_plane plane{
        0.0F, 0.0F, 0.0F,
        1.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F,
        2.0F, 2.0F,
    };
    const hpvr_wand_projection_options options{500, 0.0F, 50};
    std::array<hpvr_wand_vec2, 2> output{{{77.0F, 88.0F}, {99.0F, 111.0F}}};
    hpvr_wand_projection_report report{};
    const auto status = hpvr_wand_project_trajectory(
        samples.data(),
        static_cast<uint32_t>(samples.size()),
        &plane,
        &options,
        output.data(),
        static_cast<uint32_t>(output.size()),
        &report);

    expect(status == HPVR_WAND_PROJECTION_BUFFER_TOO_SMALL,
           "C ABI rejects a short output buffer");
    expect(report.output_point_count == 3, "C ABI reports required capacity");
    expect_near(output[0].x, 77.0F, "C ABI does not partially write x");
    expect_near(output[0].y, 88.0F, "C ABI does not partially write y");
}

void c_abi_rejects_null_arguments_and_supports_capacity_query() {
    const std::array<hpvr_wand_tracked_tip_sample, 2> samples{{
        {0, 0.0F, 0.0F, 0.0F, 1, {}},
        {100, 1.0F, 0.0F, 0.0F, 1, {}},
    }};
    const hpvr_wand_lesson_plane plane{
        0.0F, 0.0F, 0.0F,
        1.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F,
        2.0F, 2.0F,
    };
    const hpvr_wand_projection_options options{500, 0.0F, 50};
    hpvr_wand_vec2 output{};
    hpvr_wand_projection_report report{};

    expect(
        hpvr_wand_project_trajectory(
            samples.data(), 2, &plane, &options, &output, 1, nullptr) ==
            HPVR_WAND_PROJECTION_INVALID_ARGUMENT,
        "C ABI rejects a null report");
    expect(
        hpvr_wand_project_trajectory(
            nullptr, 2, &plane, &options, &output, 1, &report) ==
            HPVR_WAND_PROJECTION_INVALID_ARGUMENT,
        "C ABI rejects null non-empty input");
    expect(report.status == HPVR_WAND_PROJECTION_INVALID_ARGUMENT,
           "C ABI reports null input");
    expect(
        hpvr_wand_project_trajectory(
            samples.data(), 2, nullptr, &options, &output, 1, &report) ==
            HPVR_WAND_PROJECTION_INVALID_ARGUMENT,
        "C ABI rejects a null plane");
    expect(
        hpvr_wand_project_trajectory(
            samples.data(), 2, &plane, nullptr, &output, 1, &report) ==
            HPVR_WAND_PROJECTION_INVALID_ARGUMENT,
        "C ABI rejects null options");
    expect(
        hpvr_wand_project_trajectory(
            samples.data(), 2, &plane, &options, nullptr, 1, &report) ==
            HPVR_WAND_PROJECTION_INVALID_ARGUMENT,
        "C ABI rejects null output with nonzero capacity");
    expect(
        hpvr_wand_project_trajectory(
            samples.data(), 2, &plane, &options, nullptr, 0, &report) ==
            HPVR_WAND_PROJECTION_BUFFER_TOO_SMALL,
        "C ABI supports a zero-capacity query");
    expect(report.output_point_count == 3,
           "C ABI capacity query reports required output");
}

}  // namespace

int main() {
    projection_preserves_hp_lesson_orientation();
    invalid_planes_are_rejected();
    trajectory_reports_tracking_and_timing_rejections();
    authored_capacity_preserves_endpoints_and_order();
    time_resampling_is_cadence_independent();
    jitter_is_measured_inside_the_lesson_plane();
    resampling_telemetry_counts_replaced_points();
    invalid_pose_timestamps_still_guard_stream_order();
    tracking_loss_cancels_instead_of_bridging();
    extreme_finite_axes_normalize_stably();
    large_absolute_timestamps_keep_relative_precision();
    invalid_options_are_rejected();
    c_abi_projects_without_cpp_containers_crossing_boundary();
    c_abi_reports_required_capacity_without_partial_output();
    c_abi_rejects_null_arguments_and_supports_capacity_query();
    std::cout << "hpvr_wand_tests: all checks passed\n";
    return EXIT_SUCCESS;
}
