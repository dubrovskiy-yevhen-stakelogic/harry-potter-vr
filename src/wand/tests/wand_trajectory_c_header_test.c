#include "hpvr/wand_trajectory_c.h"

#include <stddef.h>
#include <stdint.h>

#define HPVR_C_STATIC_ASSERT(condition, name) typedef char name[(condition) ? 1 : -1]

HPVR_C_STATIC_ASSERT(sizeof(hpvr_wand_vec2) == 8, hpvr_vec2_layout);
HPVR_C_STATIC_ASSERT(
    sizeof(hpvr_wand_tracked_tip_sample) == 24,
    hpvr_sample_layout);
HPVR_C_STATIC_ASSERT(
    offsetof(hpvr_wand_tracked_tip_sample, pose_valid) == 20,
    hpvr_sample_pose_valid_offset);
HPVR_C_STATIC_ASSERT(sizeof(hpvr_wand_lesson_plane) == 44, hpvr_plane_layout);
HPVR_C_STATIC_ASSERT(
    offsetof(hpvr_wand_lesson_plane, width_m) == 36,
    hpvr_plane_width_offset);
HPVR_C_STATIC_ASSERT(
    sizeof(hpvr_wand_projection_options) == 16,
    hpvr_options_layout);
HPVR_C_STATIC_ASSERT(
    offsetof(hpvr_wand_projection_options, resampling_period_ns) == 8,
    hpvr_options_period_offset);
HPVR_C_STATIC_ASSERT(
    sizeof(hpvr_wand_projection_report) == 32,
    hpvr_report_layout);

uint32_t hpvr_wand_c_header_compile_test(void) {
    hpvr_wand_projection_report report = {0};
    const uint32_t status = hpvr_wand_project_trajectory(
        NULL,
        0,
        NULL,
        NULL,
        NULL,
        0,
        &report);
    return hpvr_wand_abi_version() + status + report.status;
}
