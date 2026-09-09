#pragma once

#include "hpvr/wand_trajectory_c.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HPVR_HP1_GESTURE_ABI_VERSION 1u
#define HPVR_HP1_GESTURE_MAX_TEMPLATE_POINTS 1024u
#define HPVR_HP1_GESTURE_MAX_SEGMENTS 4096u
#define HPVR_HP1_PASS_MARK_COUNT 10u
#define HPVR_HP1_GESTURE_NAME_CAPACITY 64u
#define HPVR_HP1_ACTOR_NAME_CAPACITY 128u
#define HPVR_HP1_ERROR_CAPACITY 256u
#define HPVR_HP1_BSP_SLICE_ABI_VERSION 1u
#define HPVR_HP1_BSP_MAX_SLICE_TRIANGLES 100000u
#define HPVR_HP1_PLAYER_START_ABI_VERSION 1u
#define HPVR_HP1_TEXTURED_BSP_ABI_VERSION 1u
#define HPVR_HP1_SKELETAL_MESH_ABI_VERSION 2u
#define HPVR_HP1_SKELETAL_ANIMATION_ABI_VERSION 1u
#define HPVR_HP1_SKELETAL_ANIMATION_FRAME_LIMIT 64u
#define HPVR_HP1_SKELETAL_TEXTURE_SIZE 256u
#define HPVR_HP1_ACTOR_VISUAL_ABI_VERSION 1u
#define HPVR_HP1_CHARACTER_MANIFEST_ABI_VERSION 1u
#define HPVR_HP1_PACKAGE_PATH_CAPACITY 512u
#define HPVR_HP1_CLASS_NAME_CAPACITY 128u

enum hpvr_hp1_profile_status {
    HPVR_HP1_PROFILE_OK = 0,
    HPVR_HP1_PROFILE_IO_ERROR = 1,
    HPVR_HP1_PROFILE_INVALID_PACKAGE = 2,
    HPVR_HP1_PROFILE_UNSUPPORTED_PACKAGE = 3,
    HPVR_HP1_PROFILE_OBJECT_NOT_FOUND = 4,
    HPVR_HP1_PROFILE_AMBIGUOUS_OBJECT = 5,
    HPVR_HP1_PROFILE_INVALID_PROFILE = 6,
    HPVR_HP1_PROFILE_INVALID_ARGUMENT = 100,
    HPVR_HP1_PROFILE_BUFFER_TOO_SMALL = 101,
    HPVR_HP1_PROFILE_ALLOCATION_FAILURE = 102,
    HPVR_HP1_PROFILE_INTERNAL_ERROR = 103,
};

typedef struct hpvr_hp1_spell_profile_report {
    uint32_t abi_version;
    uint32_t status;
    uint32_t template_point_count;
    uint32_t segment_count;
    uint32_t pass_mark_count;
    uint32_t base_package_version;
    uint32_t lesson_package_version;
    float accuracy_radius;
    float very_good_threshold;
    float very_bad_threshold;
    float default_draw_time_seconds;
    float draw_time_seconds;
    float pass_marks[HPVR_HP1_PASS_MARK_COUNT];
    char gesture_name[HPVR_HP1_GESTURE_NAME_CAPACITY];
    char lesson_actor_name[HPVR_HP1_ACTOR_NAME_CAPACITY];
    char error[HPVR_HP1_ERROR_CAPACITY];
} hpvr_hp1_spell_profile_report;

enum hpvr_hp1_score_status {
    HPVR_HP1_SCORE_OK = 0,
    HPVR_HP1_SCORE_INVALID_ACCURACY = 1,
    HPVR_HP1_SCORE_INVALID_INPUT = 2,
    HPVR_HP1_SCORE_INVALID_TEMPLATE = 3,
    HPVR_HP1_SCORE_INVALID_ARGUMENT = 100,
    HPVR_HP1_SCORE_ALLOCATION_FAILURE = 102,
    HPVR_HP1_SCORE_INTERNAL_ERROR = 103,
};

typedef struct hpvr_hp1_gesture_score_report {
    uint32_t status;
    float score;
    uint32_t input_point_count;
    uint32_t unique_input_point_count;
    uint32_t dense_template_point_count;
} hpvr_hp1_gesture_score_report;

typedef struct hpvr_hp1_bsp_slice_vertex {
    float position_m[3];
    float normal[3];
    uint32_t polygon_flags;
    uint32_t node_index;
    uint32_t surface_index;
} hpvr_hp1_bsp_slice_vertex;

typedef struct hpvr_hp1_bsp_slice_report {
    uint32_t abi_version;
    uint32_t status;
    uint32_t required_vertex_count;
    uint32_t written_vertex_count;
    uint32_t available_triangle_count;
    uint32_t selected_triangle_count;
    uint32_t omitted_triangle_count;
    uint32_t degenerate_triangle_count;
    uint32_t winding_reversal_count;
    uint32_t zero_flag_triangle_count;
    uint32_t nonzero_flag_triangle_count;
    uint32_t imported_texture_triangle_count;
    uint32_t local_texture_triangle_count;
    uint32_t no_texture_triangle_count;
    uint32_t imported_actor_triangle_count;
    uint32_t local_actor_triangle_count;
    uint32_t no_actor_triangle_count;
    float bounds_min_m[3];
    float bounds_max_m[3];
    char error[HPVR_HP1_ERROR_CAPACITY];
} hpvr_hp1_bsp_slice_report;

typedef struct hpvr_hp1_textured_bsp_vertex {
    float position_m[3];
    float normal[3];
    float texture_uv[2];
    uint32_t texture_layer;
    uint32_t polygon_flags;
    uint32_t node_index;
    uint32_t surface_index;
} hpvr_hp1_textured_bsp_vertex;

typedef struct hpvr_hp1_textured_bsp_report {
    uint32_t abi_version;
    uint32_t status;
    uint32_t required_vertex_count;
    uint32_t written_vertex_count;
    uint32_t required_texture_bytes;
    uint32_t written_texture_bytes;
    uint32_t available_triangle_count;
    uint32_t selected_triangle_count;
    uint32_t omitted_triangle_count;
    uint32_t texture_layer_width;
    uint32_t texture_layer_height;
    uint32_t texture_layer_count;
    uint32_t decoded_texture_count;
    uint32_t fallback_material_count;
    uint32_t fallback_triangle_count;
    float bounds_min_m[3];
    float bounds_max_m[3];
    char error[HPVR_HP1_ERROR_CAPACITY];
} hpvr_hp1_textured_bsp_report;

typedef struct hpvr_hp1_player_start_report {
    uint32_t abi_version;
    uint32_t status;
    uint32_t available_player_start_count;
    uint32_t selected_ordinal;
    uint32_t actor_slot_index;
    int32_t actor_reference;
    float position_m[3];
    int32_t rotation_units[3];
    uint32_t location_serialized;
    uint32_t rotation_serialized;
    char object_name[HPVR_HP1_ACTOR_NAME_CAPACITY];
    char error[HPVR_HP1_ERROR_CAPACITY];
} hpvr_hp1_player_start_report;

typedef struct hpvr_hp1_skeletal_mesh_vertex {
    float position_m[3];
    float texture_uv[2];
    uint32_t texture_layer;
    uint32_t polygon_flags;
    uint32_t face_index;
    uint32_t point_index;
} hpvr_hp1_skeletal_mesh_vertex;

typedef struct hpvr_hp1_skeletal_mesh_report {
    uint32_t abi_version;
    uint32_t status;
    uint32_t required_vertex_count;
    uint32_t written_vertex_count;
    uint32_t required_texture_bytes;
    uint32_t written_texture_bytes;
    uint32_t point_count;
    uint32_t wedge_count;
    uint32_t face_count;
    uint32_t material_count;
    uint32_t texture_layer_width;
    uint32_t texture_layer_height;
    uint32_t texture_layer_count;
    uint32_t decoded_texture_count;
    float bounds_min_m[3];
    float bounds_max_m[3];
    char error[HPVR_HP1_ERROR_CAPACITY];
} hpvr_hp1_skeletal_mesh_report;

typedef struct hpvr_hp1_skeletal_animation_point {
    float position_m[3];
} hpvr_hp1_skeletal_animation_point;

typedef struct hpvr_hp1_skeletal_animation_report {
    uint32_t abi_version;
    uint32_t status;
    uint32_t required_position_count;
    uint32_t written_position_count;
    uint32_t point_count;
    uint32_t frame_count;
    int32_t animation_reference;
    uint32_t sequence_index;
    float duration_seconds;
    char sequence_name[HPVR_HP1_ACTOR_NAME_CAPACITY];
    char error[HPVR_HP1_ERROR_CAPACITY];
} hpvr_hp1_skeletal_animation_report;

typedef struct hpvr_hp1_actor_visual_report {
    uint32_t abi_version;
    uint32_t status;
    uint32_t actor_slot_index;
    int32_t actor_reference;
    float position_m[3];
    int32_t rotation_units[3];
    float draw_scale;
    uint32_t location_serialized;
    uint32_t rotation_serialized;
    uint32_t draw_scale_serialized;
    char object_name[HPVR_HP1_ACTOR_NAME_CAPACITY];
    char error[HPVR_HP1_ERROR_CAPACITY];
} hpvr_hp1_actor_visual_report;

typedef struct hpvr_hp1_character_actor {
    uint32_t actor_slot_index;
    int32_t actor_reference;
    int32_t class_reference;
    int32_t mesh_reference;
    float position_m[3];
    int32_t rotation_units[3];
    float draw_scale;
    char object_name[HPVR_HP1_ACTOR_NAME_CAPACITY];
    char qualified_class_name[HPVR_HP1_CLASS_NAME_CAPACITY];
    char mesh_package_utf8[HPVR_HP1_PACKAGE_PATH_CAPACITY];
    char mesh_object_path[HPVR_HP1_ACTOR_NAME_CAPACITY];
} hpvr_hp1_character_actor;

typedef struct hpvr_hp1_character_manifest_report {
    uint32_t abi_version;
    uint32_t status;
    uint32_t required_actor_count;
    uint32_t written_actor_count;
    uint32_t inspected_actor_count;
    uint32_t excluded_actor_count;
    uint32_t non_character_actor_count;
    uint32_t missing_location_count;
    uint32_t unresolved_class_count;
    uint32_t missing_mesh_count;
    uint32_t hidden_actor_count;
    char error[HPVR_HP1_ERROR_CAPACITY];
} hpvr_hp1_character_manifest_report;

uint32_t hpvr_hp1_gesture_abi_version(void);

// All strings are UTF-8 and null terminated. This function never owns caller
// buffers. A short output buffer is reported before any point/segment write.
uint32_t hpvr_hp1_load_spell_profile_utf8(
    const char* base_package_utf8,
    const char* lesson_package_utf8,
    const char* gesture_name_utf8,
    const char* spell_class_name_utf8,
    hpvr_wand_vec2* output_template_points,
    uint32_t template_point_capacity,
    int32_t* output_segments,
    uint32_t segment_capacity,
    hpvr_hp1_spell_profile_report* output_report);

uint32_t hpvr_hp1_compare_gesture(
    const hpvr_wand_vec2* drawn_points,
    uint32_t drawn_point_count,
    const hpvr_wand_vec2* template_points,
    uint32_t template_point_count,
    float accuracy_radius,
    hpvr_hp1_gesture_score_report* output_report);

// Loads no assets other than the caller-selected map. A short output buffer is
// reported before any vertex write; call once with capacity zero to query the
// exact bounded size. Output contains no texture or actor payload.
uint32_t hpvr_hp1_load_bsp_slice_utf8(
    const char* map_package_utf8,
    float meters_per_unreal_unit,
    uint32_t maximum_triangle_count,
    hpvr_hp1_bsp_slice_vertex* output_vertices,
    uint32_t vertex_capacity,
    hpvr_hp1_bsp_slice_report* output_report);

// Loads a bounded textured preview from explicit user-owned packages. The
// function owns no caller buffers and performs no filesystem writes. Query
// exact vertex/pixel capacities with null buffers before the second call.
uint32_t hpvr_hp1_load_textured_bsp_utf8(
    const char* data_root_utf8,
    const char* map_package_utf8,
    float meters_per_unreal_unit,
    uint32_t maximum_triangle_count,
    hpvr_hp1_textured_bsp_vertex* output_vertices,
    uint32_t vertex_capacity,
    uint8_t* output_texture_rgba8,
    uint32_t texture_byte_capacity,
    hpvr_hp1_textured_bsp_report* output_report);

// Selects one direct Engine.PlayerStart by its order in the validated Level
// actor array. Position uses the same explicit scale and OpenXR axis mapping as
// the BSP slice. Raw Unreal rotator units are retained without interpretation.
uint32_t hpvr_hp1_load_player_start_utf8(
    const char* map_package_utf8,
    float meters_per_unreal_unit,
    uint32_t player_start_ordinal,
    hpvr_hp1_player_start_report* output_report);

// Builds one bind-pose character mesh and its material texture array directly
// from an explicit user-owned UE1 package. The function never exports assets
// or writes files. Use a null-buffer query followed by one exact-sized load.
uint32_t hpvr_hp1_load_skeletal_mesh_utf8(
    const char* mesh_package_utf8,
    int32_t mesh_reference,
    float meters_per_unreal_unit,
    hpvr_hp1_skeletal_mesh_vertex* output_vertices,
    uint32_t vertex_capacity,
    uint8_t* output_texture_rgba8,
    uint32_t texture_byte_capacity,
    hpvr_hp1_skeletal_mesh_report* output_report);

// Builds only the validated bind-pose triangle stream. This variant is for
// small shipped props whose procedural/non-P8 material cannot be decoded by
// the character texture path. No texture bytes are read or returned.
uint32_t hpvr_hp1_load_skeletal_geometry_utf8(
    const char* mesh_package_utf8,
    int32_t mesh_reference,
    float meters_per_unreal_unit,
    hpvr_hp1_skeletal_mesh_vertex* output_vertices,
    uint32_t vertex_capacity,
    hpvr_hp1_skeletal_mesh_report* output_report);

// Samples an automatically selected idle sequence into a bounded frame-major
// point array. Positions use the same OpenXR axis mapping and scale as the
// direct mesh loader. The source package remains read-only.
uint32_t hpvr_hp1_load_skeletal_animation_utf8(
    const char* mesh_package_utf8,
    int32_t mesh_reference,
    float meters_per_unreal_unit,
    uint32_t frame_count,
    hpvr_hp1_skeletal_animation_point* output_positions,
    uint32_t position_capacity,
    hpvr_hp1_skeletal_animation_report* output_report);

// Selects one exact actor reference from the validated Level actor array and
// returns only its serialized placement metadata. Class scripts/defaults and
// referenced assets are not followed by this call.
uint32_t hpvr_hp1_load_actor_visual_utf8(
    const char* map_package_utf8,
    float meters_per_unreal_unit,
    int32_t actor_reference,
    hpvr_hp1_actor_visual_report* output_report);

// Resolves every loadable character in one Level through class defaults and
// the validated package graph. A null-buffer query returns the exact actor
// capacity before the second call; no mesh or texture bytes are decoded here.
uint32_t hpvr_hp1_load_character_manifest_utf8(
    const char* data_root_utf8,
    const char* map_package_utf8,
    float meters_per_unreal_unit,
    int32_t excluded_actor_reference,
    hpvr_hp1_character_actor* output_actors,
    uint32_t actor_capacity,
    hpvr_hp1_character_manifest_report* output_report);

int64_t hpvr_hp1_lesson_resampling_period_ns(float draw_time_seconds);

#ifdef __cplusplus
}
#endif
