#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#define HPVR_WAND_NOEXCEPT noexcept
#else
#define HPVR_WAND_NOEXCEPT
#endif

enum {
    HPVR_WAND_ABI_VERSION = 1,
    HPVR_WAND_AUTHORED_POINT_CAPACITY = 500,
};

typedef enum hpvr_wand_projection_status {
    HPVR_WAND_PROJECTION_OK = 0,
    HPVR_WAND_PROJECTION_INVALID_PLANE = 1,
    HPVR_WAND_PROJECTION_INVALID_OPTIONS = 2,
    HPVR_WAND_PROJECTION_TRACKING_LOST = 3,
    HPVR_WAND_PROJECTION_INVALID_TIMING = 4,
    HPVR_WAND_PROJECTION_INVALID_ARGUMENT = 100,
    HPVR_WAND_PROJECTION_BUFFER_TOO_SMALL = 101,
    HPVR_WAND_PROJECTION_ALLOCATION_FAILURE = 102,
    HPVR_WAND_PROJECTION_INTERNAL_ERROR = 103,
} hpvr_wand_projection_status;

typedef struct hpvr_wand_vec2 {
    float x;
    float y;
} hpvr_wand_vec2;

typedef struct hpvr_wand_tracked_tip_sample {
    int64_t predicted_display_time_ns;
    float position_x_m;
    float position_y_m;
    float position_z_m;
    uint8_t pose_valid;
    uint8_t reserved[3];
} hpvr_wand_tracked_tip_sample;

typedef struct hpvr_wand_lesson_plane {
    float origin_x_m;
    float origin_y_m;
    float origin_z_m;
    float right_x;
    float right_y;
    float right_z;
    float up_x;
    float up_y;
    float up_z;
    float width_m;
    float height_m;
} hpvr_wand_lesson_plane;

typedef struct hpvr_wand_projection_options {
    uint32_t max_points;
    float minimum_tip_distance_m;
    int64_t resampling_period_ns;
} hpvr_wand_projection_options;

typedef struct hpvr_wand_projection_report {
    uint32_t status;
    uint32_t output_point_count;
    uint32_t input_sample_count;
    uint32_t invalid_sample_count;
    uint32_t non_monotonic_sample_count;
    uint32_t jitter_rejected_count;
    uint32_t resampled_away_count;
    uint32_t interpolated_point_count;
} hpvr_wand_projection_report;

uint32_t hpvr_wand_abi_version(void) HPVR_WAND_NOEXCEPT;

// The caller owns every buffer. On BUFFER_TOO_SMALL, output_point_count reports
// the required capacity and no output point is written. No C++ exception may
// cross this boundary. A null output_points with zero output_capacity is a
// supported capacity query.
uint32_t hpvr_wand_project_trajectory(
    const hpvr_wand_tracked_tip_sample* samples,
    uint32_t sample_count,
    const hpvr_wand_lesson_plane* plane,
    const hpvr_wand_projection_options* options,
    hpvr_wand_vec2* output_points,
    uint32_t output_capacity,
    hpvr_wand_projection_report* output_report) HPVR_WAND_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#undef HPVR_WAND_NOEXCEPT
