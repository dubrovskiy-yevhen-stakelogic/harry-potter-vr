#include "hpvr/hp1_gesture_c.h"

#include <stddef.h>
#include <stdint.h>

#define HPVR_HP1_C_STATIC_ASSERT(condition, name) \
    typedef char name[(condition) ? 1 : -1]

HPVR_HP1_C_STATIC_ASSERT(
    HPVR_HP1_GESTURE_ABI_VERSION == 1u,
    hpvr_hp1_abi_version_value);
HPVR_HP1_C_STATIC_ASSERT(
    sizeof(hpvr_hp1_spell_profile_report) == 536u,
    hpvr_hp1_profile_report_layout);
HPVR_HP1_C_STATIC_ASSERT(
    offsetof(hpvr_hp1_spell_profile_report, accuracy_radius) == 28u,
    hpvr_hp1_profile_float_offset);
HPVR_HP1_C_STATIC_ASSERT(
    offsetof(hpvr_hp1_spell_profile_report, gesture_name) == 88u,
    hpvr_hp1_profile_string_offset);
HPVR_HP1_C_STATIC_ASSERT(
    sizeof(hpvr_hp1_gesture_score_report) == 20u,
    hpvr_hp1_score_report_layout);
HPVR_HP1_C_STATIC_ASSERT(
    HPVR_HP1_BSP_SLICE_ABI_VERSION == 1u,
    hpvr_hp1_bsp_abi_version_value);
HPVR_HP1_C_STATIC_ASSERT(
    sizeof(hpvr_hp1_bsp_slice_vertex) == 36u,
    hpvr_hp1_bsp_vertex_layout);
HPVR_HP1_C_STATIC_ASSERT(
    offsetof(hpvr_hp1_bsp_slice_vertex, polygon_flags) == 24u,
    hpvr_hp1_bsp_vertex_flags_offset);
HPVR_HP1_C_STATIC_ASSERT(
    sizeof(hpvr_hp1_bsp_slice_report) == 348u,
    hpvr_hp1_bsp_report_layout);
HPVR_HP1_C_STATIC_ASSERT(
    offsetof(hpvr_hp1_bsp_slice_report, error) == 92u,
    hpvr_hp1_bsp_report_error_offset);
HPVR_HP1_C_STATIC_ASSERT(
    HPVR_HP1_TEXTURED_BSP_ABI_VERSION == 1u,
    hpvr_hp1_textured_bsp_abi_version_value);
HPVR_HP1_C_STATIC_ASSERT(
    sizeof(hpvr_hp1_textured_bsp_vertex) == 48u,
    hpvr_hp1_textured_bsp_vertex_layout);
HPVR_HP1_C_STATIC_ASSERT(
    offsetof(hpvr_hp1_textured_bsp_vertex, texture_uv) == 24u,
    hpvr_hp1_textured_bsp_uv_offset);
HPVR_HP1_C_STATIC_ASSERT(
    sizeof(hpvr_hp1_textured_bsp_report) == 340u,
    hpvr_hp1_textured_bsp_report_layout);
HPVR_HP1_C_STATIC_ASSERT(
    offsetof(hpvr_hp1_textured_bsp_report, error) == 84u,
    hpvr_hp1_textured_bsp_error_offset);
HPVR_HP1_C_STATIC_ASSERT(
    HPVR_HP1_PLAYER_START_ABI_VERSION == 1u,
    hpvr_hp1_player_start_abi_version_value);
HPVR_HP1_C_STATIC_ASSERT(
    sizeof(hpvr_hp1_player_start_report) == 440u,
    hpvr_hp1_player_start_report_layout);
HPVR_HP1_C_STATIC_ASSERT(
    offsetof(hpvr_hp1_player_start_report, position_m) == 24u,
    hpvr_hp1_player_start_position_offset);
HPVR_HP1_C_STATIC_ASSERT(
    offsetof(hpvr_hp1_player_start_report, object_name) == 56u,
    hpvr_hp1_player_start_name_offset);
HPVR_HP1_C_STATIC_ASSERT(
    offsetof(hpvr_hp1_player_start_report, error) == 184u,
    hpvr_hp1_player_start_error_offset);

uint32_t hpvr_hp1_gesture_c_header_compile_test(void) {
    hpvr_hp1_spell_profile_report profile = {0};
    hpvr_hp1_gesture_score_report score = {0};
    hpvr_hp1_bsp_slice_report bsp = {0};
    hpvr_hp1_textured_bsp_report textured_bsp = {0};
    hpvr_hp1_player_start_report player_start = {0};
    return hpvr_hp1_gesture_abi_version() + profile.status + score.status +
           bsp.status + textured_bsp.status + player_start.status;
}
