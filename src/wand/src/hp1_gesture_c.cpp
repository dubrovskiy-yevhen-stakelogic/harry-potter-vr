#include "hpvr/hp1_gesture_c.h"

#include "hpvr/hp1_gesture.h"
#include "hpvr/hp1_package_linker.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <limits>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t kMaximumBridgePointCount{1'000'000};

static_assert(sizeof(hpvr_hp1_spell_profile_report) == 536);
static_assert(alignof(hpvr_hp1_spell_profile_report) == 4);
static_assert(offsetof(hpvr_hp1_spell_profile_report, status) == 4);
static_assert(offsetof(hpvr_hp1_spell_profile_report, accuracy_radius) == 28);
static_assert(offsetof(hpvr_hp1_spell_profile_report, pass_marks) == 48);
static_assert(offsetof(hpvr_hp1_spell_profile_report, gesture_name) == 88);
static_assert(offsetof(hpvr_hp1_spell_profile_report, lesson_actor_name) == 152);
static_assert(offsetof(hpvr_hp1_spell_profile_report, error) == 280);
static_assert(sizeof(hpvr_hp1_gesture_score_report) == 20);
static_assert(alignof(hpvr_hp1_gesture_score_report) == 4);
static_assert(sizeof(hpvr_hp1_bsp_slice_vertex) == 36);
static_assert(alignof(hpvr_hp1_bsp_slice_vertex) == 4);
static_assert(offsetof(hpvr_hp1_bsp_slice_vertex, polygon_flags) == 24);
static_assert(offsetof(hpvr_hp1_bsp_slice_vertex, surface_index) == 32);
static_assert(sizeof(hpvr_hp1_bsp_slice_report) == 348);
static_assert(alignof(hpvr_hp1_bsp_slice_report) == 4);
static_assert(offsetof(hpvr_hp1_bsp_slice_report, bounds_min_m) == 68);
static_assert(offsetof(hpvr_hp1_bsp_slice_report, error) == 92);
static_assert(sizeof(hpvr_hp1_textured_bsp_vertex) == 48);
static_assert(alignof(hpvr_hp1_textured_bsp_vertex) == 4);
static_assert(offsetof(hpvr_hp1_textured_bsp_vertex, texture_uv) == 24);
static_assert(offsetof(hpvr_hp1_textured_bsp_vertex, texture_layer) == 32);
static_assert(offsetof(hpvr_hp1_textured_bsp_vertex, surface_index) == 44);
static_assert(sizeof(hpvr_hp1_textured_bsp_report) == 340);
static_assert(alignof(hpvr_hp1_textured_bsp_report) == 4);
static_assert(offsetof(hpvr_hp1_textured_bsp_report, bounds_min_m) == 60);
static_assert(offsetof(hpvr_hp1_textured_bsp_report, error) == 84);
static_assert(sizeof(hpvr_hp1_player_start_report) == 440);
static_assert(alignof(hpvr_hp1_player_start_report) == 4);
static_assert(offsetof(hpvr_hp1_player_start_report, position_m) == 24);
static_assert(offsetof(hpvr_hp1_player_start_report, rotation_units) == 36);
static_assert(offsetof(hpvr_hp1_player_start_report, object_name) == 56);
static_assert(offsetof(hpvr_hp1_player_start_report, error) == 184);
static_assert(sizeof(hpvr_hp1_skeletal_mesh_vertex) == 36);
static_assert(alignof(hpvr_hp1_skeletal_mesh_vertex) == 4);
static_assert(offsetof(hpvr_hp1_skeletal_mesh_vertex, texture_uv) == 12);
static_assert(offsetof(hpvr_hp1_skeletal_mesh_vertex, texture_layer) == 20);
static_assert(offsetof(hpvr_hp1_skeletal_mesh_vertex, point_index) == 32);
static_assert(sizeof(hpvr_hp1_skeletal_mesh_report) == 336);
static_assert(alignof(hpvr_hp1_skeletal_mesh_report) == 4);
static_assert(offsetof(hpvr_hp1_skeletal_mesh_report, bounds_min_m) == 56);
static_assert(offsetof(hpvr_hp1_skeletal_mesh_report, error) == 80);
static_assert(sizeof(hpvr_hp1_skeletal_animation_point) == 12);
static_assert(sizeof(hpvr_hp1_skeletal_animation_report) == 420);
static_assert(offsetof(hpvr_hp1_skeletal_animation_report, sequence_name) == 36);
static_assert(offsetof(hpvr_hp1_skeletal_animation_report, error) == 164);
static_assert(sizeof(hpvr_hp1_actor_visual_report) == 440);
static_assert(alignof(hpvr_hp1_actor_visual_report) == 4);
static_assert(offsetof(hpvr_hp1_actor_visual_report, position_m) == 16);
static_assert(offsetof(hpvr_hp1_actor_visual_report, object_name) == 56);
static_assert(offsetof(hpvr_hp1_actor_visual_report, error) == 184);
static_assert(sizeof(hpvr_hp1_character_actor) == 940);
static_assert(alignof(hpvr_hp1_character_actor) == 4);
static_assert(offsetof(hpvr_hp1_character_actor, position_m) == 16);
static_assert(offsetof(hpvr_hp1_character_actor, object_name) == 44);
static_assert(offsetof(hpvr_hp1_character_actor, mesh_package_utf8) == 300);
static_assert(sizeof(hpvr_hp1_character_manifest_report) == 300);
static_assert(alignof(hpvr_hp1_character_manifest_report) == 4);
static_assert(offsetof(hpvr_hp1_character_manifest_report, error) == 44);

template <std::size_t Capacity>
void copy_text(char (&destination)[Capacity], std::string_view source) noexcept {
    static_assert(Capacity != 0);
    const auto count = std::min(source.size(), Capacity - 1);
    if (count != 0) {
        std::memcpy(destination, source.data(), count);
    }
    destination[count] = '\0';
}

[[nodiscard]] std::filesystem::path path_from_utf8(const char* value) {
    const std::string_view bytes(value);
    std::u8string utf8;
    utf8.reserve(bytes.size());
    for (const unsigned char byte : bytes) {
        utf8.push_back(static_cast<char8_t>(byte));
    }
    return std::filesystem::path(utf8);
}

[[nodiscard]] std::string path_to_utf8(
    const std::filesystem::path& value) {
    const auto utf8 = value.u8string();
    std::string bytes;
    bytes.reserve(utf8.size());
    for (const auto byte : utf8) {
        bytes.push_back(static_cast<char>(byte));
    }
    return bytes;
}

[[nodiscard]] uint32_t profile_status(hpvr::wand::Hp1ProfileStatus status) {
    switch (status) {
        case hpvr::wand::Hp1ProfileStatus::ok:
            return HPVR_HP1_PROFILE_OK;
        case hpvr::wand::Hp1ProfileStatus::io_error:
            return HPVR_HP1_PROFILE_IO_ERROR;
        case hpvr::wand::Hp1ProfileStatus::invalid_package:
            return HPVR_HP1_PROFILE_INVALID_PACKAGE;
        case hpvr::wand::Hp1ProfileStatus::unsupported_package:
            return HPVR_HP1_PROFILE_UNSUPPORTED_PACKAGE;
        case hpvr::wand::Hp1ProfileStatus::object_not_found:
            return HPVR_HP1_PROFILE_OBJECT_NOT_FOUND;
        case hpvr::wand::Hp1ProfileStatus::ambiguous_object:
            return HPVR_HP1_PROFILE_AMBIGUOUS_OBJECT;
        case hpvr::wand::Hp1ProfileStatus::invalid_profile:
            return HPVR_HP1_PROFILE_INVALID_PROFILE;
    }
    return HPVR_HP1_PROFILE_INTERNAL_ERROR;
}

[[nodiscard]] uint32_t score_status(
    hpvr::wand::Hp1GestureScoreStatus status) {
    switch (status) {
        case hpvr::wand::Hp1GestureScoreStatus::ok:
            return HPVR_HP1_SCORE_OK;
        case hpvr::wand::Hp1GestureScoreStatus::invalid_accuracy:
            return HPVR_HP1_SCORE_INVALID_ACCURACY;
        case hpvr::wand::Hp1GestureScoreStatus::invalid_input:
            return HPVR_HP1_SCORE_INVALID_INPUT;
        case hpvr::wand::Hp1GestureScoreStatus::invalid_template:
            return HPVR_HP1_SCORE_INVALID_TEMPLATE;
    }
    return HPVR_HP1_SCORE_INTERNAL_ERROR;
}

template <typename Report>
void set_profile_error(Report& report,
                       uint32_t status,
                       std::string_view message) noexcept {
    report.status = status;
    copy_text(report.error, message);
}

}  // namespace

extern "C" uint32_t hpvr_hp1_gesture_abi_version(void) {
    return HPVR_HP1_GESTURE_ABI_VERSION;
}

extern "C" uint32_t hpvr_hp1_load_spell_profile_utf8(
    const char* base_package_utf8,
    const char* lesson_package_utf8,
    const char* gesture_name_utf8,
    const char* spell_class_name_utf8,
    hpvr_wand_vec2* output_template_points,
    uint32_t template_point_capacity,
    int32_t* output_segments,
    uint32_t segment_capacity,
    hpvr_hp1_spell_profile_report* output_report) {
    if (output_report == nullptr) {
        return HPVR_HP1_PROFILE_INVALID_ARGUMENT;
    }
    *output_report = {};
    output_report->abi_version = HPVR_HP1_GESTURE_ABI_VERSION;
    if (base_package_utf8 == nullptr || lesson_package_utf8 == nullptr ||
        gesture_name_utf8 == nullptr || spell_class_name_utf8 == nullptr ||
        *base_package_utf8 == '\0' || *lesson_package_utf8 == '\0' ||
        *gesture_name_utf8 == '\0' || *spell_class_name_utf8 == '\0' ||
        (output_template_points == nullptr && template_point_capacity != 0) ||
        (output_segments == nullptr && segment_capacity != 0)) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_INVALID_ARGUMENT,
                          "invalid C ABI argument");
        return output_report->status;
    }

    try {
        const auto profile = hpvr::wand::load_hp1_spell_profile(
            path_from_utf8(base_package_utf8),
            path_from_utf8(lesson_package_utf8),
            gesture_name_utf8,
            spell_class_name_utf8);
        output_report->status = profile_status(profile.status);
        copy_text(output_report->error, profile.error);
        if (profile.status != hpvr::wand::Hp1ProfileStatus::ok) {
            return output_report->status;
        }

        if (profile.template_points.size() >
                std::numeric_limits<uint32_t>::max() ||
            profile.segments.size() > std::numeric_limits<uint32_t>::max() ||
            profile.pass_mark_count > HPVR_HP1_PASS_MARK_COUNT) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_INTERNAL_ERROR,
                              "native profile exceeds the C ABI range");
            return output_report->status;
        }
        output_report->template_point_count =
            static_cast<uint32_t>(profile.template_points.size());
        output_report->segment_count =
            static_cast<uint32_t>(profile.segments.size());
        output_report->pass_mark_count =
            static_cast<uint32_t>(profile.pass_mark_count);
        output_report->base_package_version = profile.base_package_version;
        output_report->lesson_package_version = profile.lesson_package_version;
        output_report->accuracy_radius = profile.accuracy_radius;
        output_report->very_good_threshold = profile.very_good_threshold;
        output_report->very_bad_threshold = profile.very_bad_threshold;
        output_report->default_draw_time_seconds =
            profile.default_draw_time_seconds;
        output_report->draw_time_seconds = profile.draw_time_seconds;
        std::copy_n(profile.pass_marks.begin(),
                    profile.pass_mark_count,
                    output_report->pass_marks);
        copy_text(output_report->gesture_name, profile.gesture_name);
        copy_text(output_report->lesson_actor_name, profile.lesson_actor_name);

        if (profile.template_points.size() > template_point_capacity ||
            profile.segments.size() > segment_capacity ||
            (output_template_points == nullptr &&
             !profile.template_points.empty()) ||
            (output_segments == nullptr && !profile.segments.empty())) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_BUFFER_TOO_SMALL,
                              "caller-owned profile buffer is too small");
            return output_report->status;
        }

        for (std::size_t index = 0; index < profile.template_points.size(); ++index) {
            output_template_points[index] = {
                profile.template_points[index].x,
                profile.template_points[index].y,
            };
        }
        std::copy(profile.segments.begin(),
                  profile.segments.end(),
                  output_segments);
        return output_report->status;
    } catch (const std::bad_alloc&) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_ALLOCATION_FAILURE,
                          "allocation failed inside the native profile bridge");
    } catch (...) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_INTERNAL_ERROR,
                          "exception contained inside the native profile bridge");
    }
    return output_report->status;
}

extern "C" uint32_t hpvr_hp1_compare_gesture(
    const hpvr_wand_vec2* drawn_points,
    uint32_t drawn_point_count,
    const hpvr_wand_vec2* template_points,
    uint32_t template_point_count,
    float accuracy_radius,
    hpvr_hp1_gesture_score_report* output_report) {
    if (output_report == nullptr) {
        return HPVR_HP1_SCORE_INVALID_ARGUMENT;
    }
    *output_report = {};
    if ((drawn_points == nullptr && drawn_point_count != 0) ||
        (template_points == nullptr && template_point_count != 0) ||
        drawn_point_count > kMaximumBridgePointCount ||
        template_point_count > kMaximumBridgePointCount) {
        output_report->status = HPVR_HP1_SCORE_INVALID_ARGUMENT;
        return output_report->status;
    }

    try {
        std::vector<hpvr::wand::Vec2> drawn;
        drawn.reserve(drawn_point_count);
        for (uint32_t index = 0; index < drawn_point_count; ++index) {
            drawn.push_back({drawn_points[index].x, drawn_points[index].y});
        }
        std::vector<hpvr::wand::Vec2> authored;
        authored.reserve(template_point_count);
        for (uint32_t index = 0; index < template_point_count; ++index) {
            authored.push_back(
                {template_points[index].x, template_points[index].y});
        }
        const auto score = hpvr::wand::compare_hp1_projected_gesture(
            drawn, authored, accuracy_radius);
        output_report->status = score_status(score.status);
        output_report->score = score.score;
        output_report->input_point_count =
            static_cast<uint32_t>(score.input_point_count);
        output_report->unique_input_point_count =
            static_cast<uint32_t>(score.unique_input_point_count);
        output_report->dense_template_point_count =
            static_cast<uint32_t>(score.dense_template_point_count);
    } catch (const std::bad_alloc&) {
        output_report->status = HPVR_HP1_SCORE_ALLOCATION_FAILURE;
    } catch (...) {
        output_report->status = HPVR_HP1_SCORE_INTERNAL_ERROR;
    }
    return output_report->status;
}

extern "C" uint32_t hpvr_hp1_load_bsp_slice_utf8(
    const char* map_package_utf8,
    float meters_per_unreal_unit,
    uint32_t maximum_triangle_count,
    hpvr_hp1_bsp_slice_vertex* output_vertices,
    uint32_t vertex_capacity,
    hpvr_hp1_bsp_slice_report* output_report) {
    if (output_report == nullptr) {
        return HPVR_HP1_PROFILE_INVALID_ARGUMENT;
    }
    *output_report = {};
    output_report->abi_version = HPVR_HP1_BSP_SLICE_ABI_VERSION;
    if (map_package_utf8 == nullptr || *map_package_utf8 == '\0' ||
        !std::isfinite(meters_per_unreal_unit) ||
        meters_per_unreal_unit <= 0.0F || maximum_triangle_count == 0 ||
        maximum_triangle_count > HPVR_HP1_BSP_MAX_SLICE_TRIANGLES ||
        (output_vertices == nullptr && vertex_capacity != 0)) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_INVALID_ARGUMENT,
                          "invalid BSP slice C ABI argument");
        return output_report->status;
    }

    try {
        const auto topology = hpvr::wand::load_hp1_bsp_topology(
            path_from_utf8(map_package_utf8));
        output_report->status = profile_status(topology.status);
        copy_text(output_report->error, topology.error);
        if (topology.status != hpvr::wand::Hp1ProfileStatus::ok) {
            return output_report->status;
        }
        const auto mesh = hpvr::wand::build_hp1_bsp_triangle_mesh(
            topology, meters_per_unreal_unit);
        output_report->status = profile_status(mesh.status);
        copy_text(output_report->error, mesh.error);
        if (mesh.status != hpvr::wand::Hp1ProfileStatus::ok) {
            return output_report->status;
        }
        if (mesh.triangles.size() > std::numeric_limits<uint32_t>::max()) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_INTERNAL_ERROR,
                              "native BSP mesh exceeds the C ABI range");
            return output_report->status;
        }
        const auto selected_count = std::min<std::size_t>(
            mesh.triangles.size(), maximum_triangle_count);
        const auto required_vertices = selected_count * 3;
        output_report->available_triangle_count =
            static_cast<uint32_t>(mesh.triangles.size());
        output_report->selected_triangle_count =
            static_cast<uint32_t>(selected_count);
        output_report->omitted_triangle_count = static_cast<uint32_t>(
            mesh.triangles.size() - selected_count);
        output_report->required_vertex_count =
            static_cast<uint32_t>(required_vertices);
        output_report->degenerate_triangle_count =
            static_cast<uint32_t>(mesh.degenerate_triangle_count);
        output_report->winding_reversal_count =
            static_cast<uint32_t>(mesh.reversed_winding_count);

        bool have_bounds = false;
        for (std::size_t index = 0; index < selected_count; ++index) {
            const auto& triangle = mesh.triangles[index];
            const auto& surface = topology.surfaces[triangle.surface_index];
            if (surface.polygon_flags == 0) {
                ++output_report->zero_flag_triangle_count;
            } else {
                ++output_report->nonzero_flag_triangle_count;
            }
            if (surface.texture_reference < 0) {
                ++output_report->imported_texture_triangle_count;
            } else if (surface.texture_reference > 0) {
                ++output_report->local_texture_triangle_count;
            } else {
                ++output_report->no_texture_triangle_count;
            }
            if (surface.actor_reference < 0) {
                ++output_report->imported_actor_triangle_count;
            } else if (surface.actor_reference > 0) {
                ++output_report->local_actor_triangle_count;
            } else {
                ++output_report->no_actor_triangle_count;
            }
            for (const auto position : triangle.positions_m) {
                const std::array components{position.x, position.y, position.z};
                for (std::size_t axis = 0; axis < components.size(); ++axis) {
                    if (!have_bounds) {
                        output_report->bounds_min_m[axis] = components[axis];
                        output_report->bounds_max_m[axis] = components[axis];
                    } else {
                        output_report->bounds_min_m[axis] = std::min(
                            output_report->bounds_min_m[axis], components[axis]);
                        output_report->bounds_max_m[axis] = std::max(
                            output_report->bounds_max_m[axis], components[axis]);
                    }
                }
                have_bounds = true;
            }
        }

        if (required_vertices > vertex_capacity ||
            (output_vertices == nullptr && required_vertices != 0)) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_BUFFER_TOO_SMALL,
                              "caller-owned BSP slice buffer is too small");
            return output_report->status;
        }
        for (std::size_t index = 0; index < selected_count; ++index) {
            const auto& triangle = mesh.triangles[index];
            const auto& surface = topology.surfaces[triangle.surface_index];
            for (std::size_t corner = 0; corner < 3; ++corner) {
                auto& output = output_vertices[index * 3 + corner];
                const auto position = triangle.positions_m[corner];
                output = {
                    {position.x, position.y, position.z},
                    {triangle.normal.x, triangle.normal.y, triangle.normal.z},
                    surface.polygon_flags,
                    triangle.node_index,
                    triangle.surface_index,
                };
            }
        }
        output_report->written_vertex_count =
            static_cast<uint32_t>(required_vertices);
        return output_report->status;
    } catch (const std::bad_alloc&) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_ALLOCATION_FAILURE,
                          "allocation failed inside the BSP slice bridge");
    } catch (...) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_INTERNAL_ERROR,
                          "exception contained inside the BSP slice bridge");
    }
    return output_report->status;
}

extern "C" uint32_t hpvr_hp1_load_textured_bsp_utf8(
    const char* data_root_utf8,
    const char* map_package_utf8,
    float meters_per_unreal_unit,
    uint32_t maximum_triangle_count,
    hpvr_hp1_textured_bsp_vertex* output_vertices,
    uint32_t vertex_capacity,
    uint8_t* output_texture_rgba8,
    uint32_t texture_byte_capacity,
    hpvr_hp1_textured_bsp_report* output_report) {
    if (output_report == nullptr) {
        return HPVR_HP1_PROFILE_INVALID_ARGUMENT;
    }
    *output_report = {};
    output_report->abi_version = HPVR_HP1_TEXTURED_BSP_ABI_VERSION;
    if (data_root_utf8 == nullptr || *data_root_utf8 == '\0' ||
        map_package_utf8 == nullptr || *map_package_utf8 == '\0' ||
        !std::isfinite(meters_per_unreal_unit) ||
        meters_per_unreal_unit <= 0.0F || maximum_triangle_count == 0 ||
        maximum_triangle_count > HPVR_HP1_BSP_MAX_SLICE_TRIANGLES ||
        (output_vertices == nullptr && vertex_capacity != 0) ||
        (output_texture_rgba8 == nullptr && texture_byte_capacity != 0)) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_INVALID_ARGUMENT,
                          "invalid textured BSP C ABI argument");
        return output_report->status;
    }
    try {
        const auto scene = hpvr::wand::build_hp1_textured_bsp_scene(
            path_from_utf8(data_root_utf8),
            path_from_utf8(map_package_utf8),
            meters_per_unreal_unit,
            maximum_triangle_count);
        output_report->status = profile_status(scene.status);
        copy_text(output_report->error, scene.error);
        if (scene.status != hpvr::wand::Hp1ProfileStatus::ok) {
            return output_report->status;
        }
        const auto fits_u32 = [](std::size_t value) {
            return value <= std::numeric_limits<uint32_t>::max();
        };
        if (!fits_u32(scene.vertices.size()) ||
            !fits_u32(scene.texture_rgba8.size()) ||
            !fits_u32(scene.available_triangle_count) ||
            !fits_u32(scene.selected_triangle_count) ||
            !fits_u32(scene.omitted_triangle_count) ||
            !fits_u32(scene.decoded_texture_count) ||
            !fits_u32(scene.fallback_material_count) ||
            !fits_u32(scene.fallback_triangle_count)) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_INTERNAL_ERROR,
                              "textured BSP scene exceeds the C ABI range");
            return output_report->status;
        }
        output_report->required_vertex_count =
            static_cast<uint32_t>(scene.vertices.size());
        output_report->required_texture_bytes =
            static_cast<uint32_t>(scene.texture_rgba8.size());
        output_report->available_triangle_count =
            static_cast<uint32_t>(scene.available_triangle_count);
        output_report->selected_triangle_count =
            static_cast<uint32_t>(scene.selected_triangle_count);
        output_report->omitted_triangle_count =
            static_cast<uint32_t>(scene.omitted_triangle_count);
        output_report->texture_layer_width = scene.texture_layer_width;
        output_report->texture_layer_height = scene.texture_layer_height;
        output_report->texture_layer_count = scene.texture_layer_count;
        output_report->decoded_texture_count =
            static_cast<uint32_t>(scene.decoded_texture_count);
        output_report->fallback_material_count =
            static_cast<uint32_t>(scene.fallback_material_count);
        output_report->fallback_triangle_count =
            static_cast<uint32_t>(scene.fallback_triangle_count);

        bool have_bounds = false;
        for (const auto& vertex : scene.vertices) {
            const std::array components{
                vertex.position_m.x,
                vertex.position_m.y,
                vertex.position_m.z,
            };
            for (std::size_t axis = 0; axis < components.size(); ++axis) {
                if (!have_bounds) {
                    output_report->bounds_min_m[axis] = components[axis];
                    output_report->bounds_max_m[axis] = components[axis];
                } else {
                    output_report->bounds_min_m[axis] = std::min(
                        output_report->bounds_min_m[axis], components[axis]);
                    output_report->bounds_max_m[axis] = std::max(
                        output_report->bounds_max_m[axis], components[axis]);
                }
            }
            have_bounds = true;
        }

        if (scene.vertices.size() > vertex_capacity ||
            scene.texture_rgba8.size() > texture_byte_capacity ||
            (output_vertices == nullptr && !scene.vertices.empty()) ||
            (output_texture_rgba8 == nullptr &&
             !scene.texture_rgba8.empty())) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_BUFFER_TOO_SMALL,
                              "caller-owned textured BSP buffers are too small");
            return output_report->status;
        }
        for (std::size_t index = 0; index < scene.vertices.size(); ++index) {
            const auto& source = scene.vertices[index];
            output_vertices[index] = {
                {source.position_m.x, source.position_m.y,
                 source.position_m.z},
                {source.normal.x, source.normal.y, source.normal.z},
                {source.texture_uv[0], source.texture_uv[1]},
                source.texture_layer,
                source.polygon_flags,
                source.node_index,
                source.surface_index,
            };
        }
        std::copy(scene.texture_rgba8.begin(),
                  scene.texture_rgba8.end(), output_texture_rgba8);
        output_report->written_vertex_count =
            output_report->required_vertex_count;
        output_report->written_texture_bytes =
            output_report->required_texture_bytes;
        return output_report->status;
    } catch (const std::bad_alloc&) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_ALLOCATION_FAILURE,
                          "allocation failed inside the textured BSP bridge");
    } catch (...) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_INTERNAL_ERROR,
                          "exception contained inside the textured BSP bridge");
    }
    return output_report->status;
}

extern "C" uint32_t hpvr_hp1_load_skeletal_mesh_utf8(
    const char* mesh_package_utf8,
    int32_t mesh_reference,
    float meters_per_unreal_unit,
    hpvr_hp1_skeletal_mesh_vertex* output_vertices,
    uint32_t vertex_capacity,
    uint8_t* output_texture_rgba8,
    uint32_t texture_byte_capacity,
    hpvr_hp1_skeletal_mesh_report* output_report) {
    if (output_report == nullptr) {
        return HPVR_HP1_PROFILE_INVALID_ARGUMENT;
    }
    *output_report = {};
    output_report->abi_version = HPVR_HP1_SKELETAL_MESH_ABI_VERSION;
    if (mesh_package_utf8 == nullptr || *mesh_package_utf8 == '\0' ||
        mesh_reference <= 0 || !std::isfinite(meters_per_unreal_unit) ||
        meters_per_unreal_unit <= 0.0F ||
        (output_vertices == nullptr && vertex_capacity != 0) ||
        (output_texture_rgba8 == nullptr && texture_byte_capacity != 0)) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_INVALID_ARGUMENT,
                          "invalid SkeletalMesh C ABI argument");
        return output_report->status;
    }
    try {
        const auto path = path_from_utf8(mesh_package_utf8);
        const auto mesh = hpvr::wand::build_hp1_skeletal_triangle_mesh(
            path, mesh_reference, meters_per_unreal_unit);
        output_report->status = profile_status(mesh.status);
        copy_text(output_report->error, mesh.error);
        if (mesh.status != hpvr::wand::Hp1ProfileStatus::ok) {
            return output_report->status;
        }
        const auto fits_u32 = [](std::size_t value) {
            return value <= std::numeric_limits<uint32_t>::max();
        };
        const auto& census = mesh.census;
        if (!fits_u32(mesh.vertices.size()) ||
            !fits_u32(census.point_count) ||
            !fits_u32(census.lod_wedge_count) ||
            !fits_u32(census.face_count) ||
            !fits_u32(census.material_count) ||
            census.material_count == 0 ||
            census.material_count > std::numeric_limits<uint32_t>::max()) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_INTERNAL_ERROR,
                              "SkeletalMesh result exceeds the C ABI range");
            return output_report->status;
        }
        constexpr std::size_t texture_size =
            HPVR_HP1_SKELETAL_TEXTURE_SIZE;
        constexpr std::size_t layer_bytes = texture_size * texture_size * 4U;
        if (census.material_count >
            std::numeric_limits<std::size_t>::max() / layer_bytes) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_INTERNAL_ERROR,
                              "SkeletalMesh texture array size overflowed");
            return output_report->status;
        }
        std::vector<std::uint8_t> texture_array(
            census.material_count * layer_bytes);
        for (std::size_t material_index = 0;
             material_index < census.material_texture_indices.size();
             ++material_index) {
            const auto texture_slot =
                census.material_texture_indices[material_index];
            if (texture_slot < 0 ||
                static_cast<std::size_t>(texture_slot) >=
                    census.texture_references.size()) {
                set_profile_error(*output_report,
                                  HPVR_HP1_PROFILE_INVALID_PROFILE,
                                  "SkeletalMesh material texture slot is invalid");
                return output_report->status;
            }
            const auto texture_reference = census.texture_references[
                static_cast<std::size_t>(texture_slot)];
            if (texture_reference <= 0) {
                set_profile_error(*output_report,
                                  HPVR_HP1_PROFILE_INVALID_PROFILE,
                                  "SkeletalMesh material is not a local texture");
                return output_report->status;
            }
            const bool masked=std::ranges::any_of(mesh.vertices,[&](const auto& v){
                return v.material_index==material_index&&(v.polygon_flags&0x2U)!=0;});
            const auto texture = hpvr::wand::load_hp1_p8_texture(
                path, texture_reference, masked);
            if (texture.status != hpvr::wand::Hp1ProfileStatus::ok ||
                texture.mips.empty()) {
                output_report->status = profile_status(texture.status);
                copy_text(output_report->error,
                          "SkeletalMesh P8 texture decode failed: " +
                              texture.error);
                return output_report->status;
            }
            const auto width = texture.mips.front().width;
            const auto height = texture.mips.front().height;
            const auto expected_bytes =
                static_cast<std::size_t>(width) * height * 4U;
            if (width == 0 || height == 0 ||
                texture.rgba8.size() != expected_bytes) {
                set_profile_error(*output_report,
                                  HPVR_HP1_PROFILE_INVALID_PROFILE,
                                  "SkeletalMesh P8 top mip size is invalid");
                return output_report->status;
            }
            const auto destination_base = material_index * layer_bytes;
            for (std::size_t y = 0; y < texture_size; ++y) {
                const auto source_y = y * height / texture_size;
                for (std::size_t x = 0; x < texture_size; ++x) {
                    const auto source_x = x * width / texture_size;
                    const auto source =
                        (source_y * width + source_x) * 4U;
                    const auto destination =
                        destination_base + (y * texture_size + x) * 4U;
                    std::copy_n(texture.rgba8.data() + source, 4,
                                texture_array.data() + destination);
                }
            }
        }
        if (!fits_u32(texture_array.size())) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_INTERNAL_ERROR,
                              "SkeletalMesh texture bytes exceed uint32");
            return output_report->status;
        }

        output_report->required_vertex_count =
            static_cast<uint32_t>(mesh.vertices.size());
        output_report->required_texture_bytes =
            static_cast<uint32_t>(texture_array.size());
        output_report->point_count =
            static_cast<uint32_t>(census.point_count);
        output_report->wedge_count =
            static_cast<uint32_t>(census.lod_wedge_count);
        output_report->face_count =
            static_cast<uint32_t>(census.face_count);
        output_report->material_count =
            static_cast<uint32_t>(census.material_count);
        output_report->texture_layer_width =
            HPVR_HP1_SKELETAL_TEXTURE_SIZE;
        output_report->texture_layer_height =
            HPVR_HP1_SKELETAL_TEXTURE_SIZE;
        output_report->texture_layer_count =
            static_cast<uint32_t>(census.material_count);
        output_report->decoded_texture_count =
            static_cast<uint32_t>(census.material_count);
        output_report->bounds_min_m[0] = mesh.bounds_min_m.x;
        output_report->bounds_min_m[1] = mesh.bounds_min_m.y;
        output_report->bounds_min_m[2] = mesh.bounds_min_m.z;
        output_report->bounds_max_m[0] = mesh.bounds_max_m.x;
        output_report->bounds_max_m[1] = mesh.bounds_max_m.y;
        output_report->bounds_max_m[2] = mesh.bounds_max_m.z;

        if (mesh.vertices.size() > vertex_capacity ||
            texture_array.size() > texture_byte_capacity ||
            output_vertices == nullptr || output_texture_rgba8 == nullptr) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_BUFFER_TOO_SMALL,
                              "caller-owned SkeletalMesh buffers are too small");
            return output_report->status;
        }
        for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
            const auto& source = mesh.vertices[index];
            output_vertices[index] = {
                {source.position_m.x, source.position_m.y,
                 source.position_m.z},
                {source.texture_uv[0], source.texture_uv[1]},
                source.material_index,
                source.polygon_flags,
                source.face_index,
                source.point_index,
            };
        }
        std::copy(texture_array.begin(), texture_array.end(),
                  output_texture_rgba8);
        output_report->written_vertex_count =
            output_report->required_vertex_count;
        output_report->written_texture_bytes =
            output_report->required_texture_bytes;
        output_report->status = HPVR_HP1_PROFILE_OK;
        output_report->error[0] = '\0';
        return output_report->status;
    } catch (const std::bad_alloc&) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_ALLOCATION_FAILURE,
                          "allocation failed inside the SkeletalMesh bridge");
    } catch (...) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_INTERNAL_ERROR,
                          "exception contained inside the SkeletalMesh bridge");
    }
    return output_report->status;
}

extern "C" uint32_t hpvr_hp1_load_skeletal_geometry_utf8(
    const char* mesh_package_utf8,
    int32_t mesh_reference,
    float meters_per_unreal_unit,
    hpvr_hp1_skeletal_mesh_vertex* output_vertices,
    uint32_t vertex_capacity,
    hpvr_hp1_skeletal_mesh_report* output_report) {
    if (output_report == nullptr) {
        return HPVR_HP1_PROFILE_INVALID_ARGUMENT;
    }
    *output_report = {};
    output_report->abi_version = HPVR_HP1_SKELETAL_MESH_ABI_VERSION;
    if (mesh_package_utf8 == nullptr || *mesh_package_utf8 == '\0' ||
        mesh_reference <= 0 || !std::isfinite(meters_per_unreal_unit) ||
        meters_per_unreal_unit <= 0.0F ||
        (output_vertices == nullptr && vertex_capacity != 0)) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_INVALID_ARGUMENT,
                          "invalid skeletal geometry C ABI argument");
        return output_report->status;
    }
    try {
        const auto mesh = hpvr::wand::build_hp1_skeletal_triangle_mesh(
            path_from_utf8(mesh_package_utf8), mesh_reference,
            meters_per_unreal_unit);
        output_report->status = profile_status(mesh.status);
        copy_text(output_report->error, mesh.error);
        if (mesh.status != hpvr::wand::Hp1ProfileStatus::ok) {
            return output_report->status;
        }
        const auto& census = mesh.census;
        const auto fits_u32 = [](std::size_t value) {
            return value <= std::numeric_limits<uint32_t>::max();
        };
        if (!fits_u32(mesh.vertices.size()) ||
            !fits_u32(census.point_count) ||
            !fits_u32(census.lod_wedge_count) ||
            !fits_u32(census.face_count) ||
            !fits_u32(census.material_count)) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_INTERNAL_ERROR,
                              "skeletal geometry result exceeds the C ABI range");
            return output_report->status;
        }
        output_report->required_vertex_count =
            static_cast<uint32_t>(mesh.vertices.size());
        output_report->point_count = static_cast<uint32_t>(census.point_count);
        output_report->wedge_count =
            static_cast<uint32_t>(census.lod_wedge_count);
        output_report->face_count = static_cast<uint32_t>(census.face_count);
        output_report->material_count =
            static_cast<uint32_t>(census.material_count);
        output_report->bounds_min_m[0] = mesh.bounds_min_m.x;
        output_report->bounds_min_m[1] = mesh.bounds_min_m.y;
        output_report->bounds_min_m[2] = mesh.bounds_min_m.z;
        output_report->bounds_max_m[0] = mesh.bounds_max_m.x;
        output_report->bounds_max_m[1] = mesh.bounds_max_m.y;
        output_report->bounds_max_m[2] = mesh.bounds_max_m.z;

        if (mesh.vertices.size() > vertex_capacity ||
            output_vertices == nullptr) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_BUFFER_TOO_SMALL,
                              "caller-owned skeletal geometry buffer is too small");
            return output_report->status;
        }
        for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
            const auto& source = mesh.vertices[index];
            output_vertices[index] = {
                {source.position_m.x, source.position_m.y,
                 source.position_m.z},
                {source.texture_uv[0], source.texture_uv[1]},
                source.material_index,
                source.polygon_flags,
                source.face_index,
                source.point_index,
            };
        }
        output_report->written_vertex_count =
            output_report->required_vertex_count;
        output_report->status = HPVR_HP1_PROFILE_OK;
        output_report->error[0] = '\0';
        return output_report->status;
    } catch (const std::bad_alloc&) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_ALLOCATION_FAILURE,
                          "allocation failed inside the skeletal geometry bridge");
    } catch (...) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_INTERNAL_ERROR,
                          "exception contained inside the skeletal geometry bridge");
    }
    return output_report->status;
}

extern "C" uint32_t hpvr_hp1_load_skeletal_animation_utf8(
    const char* mesh_package_utf8,
    int32_t mesh_reference,
    float meters_per_unreal_unit,
    uint32_t frame_count,
    hpvr_hp1_skeletal_animation_point* output_positions,
    uint32_t position_capacity,
    hpvr_hp1_skeletal_animation_report* output_report) {
    if (output_report == nullptr) {
        return HPVR_HP1_PROFILE_INVALID_ARGUMENT;
    }
    *output_report = {};
    output_report->abi_version = HPVR_HP1_SKELETAL_ANIMATION_ABI_VERSION;
    if (mesh_package_utf8 == nullptr || *mesh_package_utf8 == '\0' ||
        mesh_reference <= 0 || !std::isfinite(meters_per_unreal_unit) ||
        meters_per_unreal_unit <= 0.0F || frame_count == 0 ||
        frame_count > HPVR_HP1_SKELETAL_ANIMATION_FRAME_LIMIT ||
        (output_positions == nullptr && position_capacity != 0)) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_INVALID_ARGUMENT,
                          "invalid SkeletalAnimation C ABI argument");
        return output_report->status;
    }
    try {
        const auto path = path_from_utf8(mesh_package_utf8);
        const auto skin =
            hpvr::wand::load_hp1_skeletal_skin(path, mesh_reference);
        if (skin.status != hpvr::wand::Hp1ProfileStatus::ok) {
            output_report->status = profile_status(skin.status);
            copy_text(output_report->error, skin.error);
            return output_report->status;
        }
        const auto animation_reference = skin.census.animation_reference;
        if (animation_reference <= 0) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_INVALID_PROFILE,
                              "SkeletalMesh animation is not a local export");
            return output_report->status;
        }
        const auto animation =
            hpvr::wand::load_hp1_animation(path, animation_reference);
        if (animation.status != hpvr::wand::Hp1ProfileStatus::ok ||
            animation.sequences.empty()) {
            output_report->status = profile_status(animation.status);
            copy_text(output_report->error,
                      "SkeletalAnimation decode failed: " + animation.error);
            return output_report->status;
        }
        const auto equals_fold = [](std::string_view left,
                                    std::string_view right) {
            if (left.size() != right.size()) {
                return false;
            }
            for (std::size_t index = 0; index < left.size(); ++index) {
                const auto fold = [](char value) {
                    return value >= 'A' && value <= 'Z'
                               ? static_cast<char>(value - 'A' + 'a')
                               : value;
                };
                if (fold(left[index]) != fold(right[index])) {
                    return false;
                }
            }
            return true;
        };
        std::size_t sequence_index = 0;
        bool found = false;
        for (const auto preferred :
             {"breathe", "breath", "float", "look", "walk"}) {
            for (std::size_t index = 0;
                 index < animation.sequences.size();
                 ++index) {
                if (equals_fold(animation.sequences[index].name, preferred)) {
                    sequence_index = index;
                    found = true;
                    break;
                }
            }
            if (found) {
                break;
            }
        }
        if (sequence_index >= animation.moves.size() ||
            animation.moves[sequence_index].track_time <= 0.0F) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_INVALID_PROFILE,
                              "SkeletalAnimation idle sequence has no move");
            return output_report->status;
        }
        if (skin.points.size() >
            std::numeric_limits<std::size_t>::max() / frame_count) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_INTERNAL_ERROR,
                              "SkeletalAnimation frame array overflowed");
            return output_report->status;
        }
        const auto required = skin.points.size() * frame_count;
        if (required > std::numeric_limits<uint32_t>::max()) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_INTERNAL_ERROR,
                              "SkeletalAnimation positions exceed uint32");
            return output_report->status;
        }
        output_report->required_position_count =
            static_cast<uint32_t>(required);
        output_report->point_count =
            static_cast<uint32_t>(skin.points.size());
        output_report->frame_count = frame_count;
        output_report->animation_reference = animation_reference;
        output_report->sequence_index =
            static_cast<uint32_t>(sequence_index);
        output_report->duration_seconds =
            animation.moves[sequence_index].track_time;
        copy_text(output_report->sequence_name,
                  animation.sequences[sequence_index].name);

        std::vector<hpvr_hp1_skeletal_animation_point> positions;
        positions.reserve(required);
        for (uint32_t frame = 0; frame < frame_count; ++frame) {
            const float elapsed =
                output_report->duration_seconds *
                static_cast<float>(frame) / static_cast<float>(frame_count);
            const auto pose = hpvr::wand::sample_hp1_skeletal_animation(
                skin, animation, sequence_index, elapsed);
            if (pose.status != hpvr::wand::Hp1ProfileStatus::ok ||
                pose.points.size() != skin.points.size()) {
                output_report->status = profile_status(pose.status);
                copy_text(output_report->error,
                          "SkeletalAnimation sample failed: " + pose.error);
                return output_report->status;
            }
            for (const auto point : pose.points) {
                positions.push_back({{
                    point.y * meters_per_unreal_unit,
                    point.z * meters_per_unreal_unit,
                    point.x * meters_per_unreal_unit,
                }});
            }
        }
        if (positions.size() > position_capacity ||
            output_positions == nullptr) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_BUFFER_TOO_SMALL,
                              "caller-owned animation buffer is too small");
            return output_report->status;
        }
        std::copy(
            positions.begin(), positions.end(), output_positions);
        output_report->written_position_count =
            output_report->required_position_count;
        output_report->status = HPVR_HP1_PROFILE_OK;
        output_report->error[0] = '\0';
        return output_report->status;
    } catch (const std::bad_alloc&) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_ALLOCATION_FAILURE,
                          "allocation failed inside animation bridge");
    } catch (...) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_INTERNAL_ERROR,
                          "exception contained inside animation bridge");
    }
    return output_report->status;
}

extern "C" uint32_t hpvr_hp1_load_actor_visual_utf8(
    const char* map_package_utf8,
    float meters_per_unreal_unit,
    int32_t actor_reference,
    hpvr_hp1_actor_visual_report* output_report) {
    if (output_report == nullptr) {
        return HPVR_HP1_PROFILE_INVALID_ARGUMENT;
    }
    *output_report = {};
    output_report->abi_version = HPVR_HP1_ACTOR_VISUAL_ABI_VERSION;
    if (map_package_utf8 == nullptr || *map_package_utf8 == '\0' ||
        actor_reference <= 0 || !std::isfinite(meters_per_unreal_unit) ||
        meters_per_unreal_unit <= 0.0F) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_INVALID_ARGUMENT,
                          "invalid actor-visual C ABI argument");
        return output_report->status;
    }
    try {
        const auto census = hpvr::wand::inspect_hp1_actor_visuals(
            path_from_utf8(map_package_utf8));
        output_report->status = profile_status(census.status);
        copy_text(output_report->error, census.error);
        if (census.status != hpvr::wand::Hp1ProfileStatus::ok) {
            return output_report->status;
        }
        const auto match = std::ranges::find_if(
            census.actors, [actor_reference](const auto& actor) {
                return actor.actor_reference == actor_reference;
            });
        if (match == census.actors.end()) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_OBJECT_NOT_FOUND,
                              "actor reference is absent from the Level array");
            return output_report->status;
        }
        if (match->actor_slot_index > std::numeric_limits<uint32_t>::max()) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_INTERNAL_ERROR,
                              "actor slot exceeds the C ABI range");
            return output_report->status;
        }
        output_report->actor_slot_index =
            static_cast<uint32_t>(match->actor_slot_index);
        output_report->actor_reference = match->actor_reference;
        output_report->position_m[0] =
            match->location_unreal.y * meters_per_unreal_unit;
        output_report->position_m[1] =
            match->location_unreal.z * meters_per_unreal_unit;
        output_report->position_m[2] =
            -match->location_unreal.x * meters_per_unreal_unit;
        std::ranges::copy(match->rotation_units,
                          output_report->rotation_units);
        output_report->draw_scale = match->draw_scale;
        output_report->location_serialized =
            match->location_serialized ? 1U : 0U;
        output_report->rotation_serialized =
            match->rotation_serialized ? 1U : 0U;
        output_report->draw_scale_serialized =
            match->draw_scale_serialized ? 1U : 0U;
        copy_text(output_report->object_name, match->object_name);
        if (!std::ranges::all_of(
                output_report->position_m,
                [](float component) { return std::isfinite(component); }) ||
            !std::isfinite(output_report->draw_scale) ||
            output_report->draw_scale <= 0.0F) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_INVALID_PROFILE,
                              "actor placement contains invalid numeric data");
        }
    } catch (const std::bad_alloc&) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_ALLOCATION_FAILURE,
                          "allocation failed inside the actor-visual bridge");
    } catch (...) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_INTERNAL_ERROR,
                          "exception contained inside the actor-visual bridge");
    }
    return output_report->status;
}

extern "C" uint32_t hpvr_hp1_load_character_manifest_utf8(
    const char* data_root_utf8,
    const char* map_package_utf8,
    float meters_per_unreal_unit,
    int32_t excluded_actor_reference,
    hpvr_hp1_character_actor* output_actors,
    uint32_t actor_capacity,
    hpvr_hp1_character_manifest_report* output_report) {
    if (output_report == nullptr) {
        return HPVR_HP1_PROFILE_INVALID_ARGUMENT;
    }
    *output_report = {};
    output_report->abi_version = HPVR_HP1_CHARACTER_MANIFEST_ABI_VERSION;
    if (data_root_utf8 == nullptr || *data_root_utf8 == '\0' ||
        map_package_utf8 == nullptr || *map_package_utf8 == '\0' ||
        !std::isfinite(meters_per_unreal_unit) ||
        meters_per_unreal_unit <= 0.0F || excluded_actor_reference < 0 ||
        (actor_capacity != 0 && output_actors == nullptr)) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_INVALID_ARGUMENT,
                          "invalid character-manifest C ABI argument");
        return output_report->status;
    }
    try {
        const auto manifest = hpvr::wand::build_hp1_character_manifest(
            path_from_utf8(data_root_utf8),
            path_from_utf8(map_package_utf8),
            excluded_actor_reference);
        output_report->status = profile_status(manifest.status);
        copy_text(output_report->error, manifest.error);
        if (manifest.status != hpvr::wand::Hp1ProfileStatus::ok) {
            return output_report->status;
        }
        const auto fits_u32 = [](std::size_t value) {
            return value <= std::numeric_limits<uint32_t>::max();
        };
        if (!fits_u32(manifest.actors.size()) ||
            !fits_u32(manifest.inspected_actor_count) ||
            !fits_u32(manifest.excluded_actor_count) ||
            !fits_u32(manifest.non_character_actor_count) ||
            !fits_u32(manifest.missing_location_count) ||
            !fits_u32(manifest.unresolved_class_count) ||
            !fits_u32(manifest.missing_mesh_count) ||
            !fits_u32(manifest.hidden_actor_count)) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_INTERNAL_ERROR,
                              "character manifest exceeds the C ABI range");
            return output_report->status;
        }
        output_report->required_actor_count =
            static_cast<uint32_t>(manifest.actors.size());
        output_report->inspected_actor_count =
            static_cast<uint32_t>(manifest.inspected_actor_count);
        output_report->excluded_actor_count =
            static_cast<uint32_t>(manifest.excluded_actor_count);
        output_report->non_character_actor_count =
            static_cast<uint32_t>(manifest.non_character_actor_count);
        output_report->missing_location_count =
            static_cast<uint32_t>(manifest.missing_location_count);
        output_report->unresolved_class_count =
            static_cast<uint32_t>(manifest.unresolved_class_count);
        output_report->missing_mesh_count =
            static_cast<uint32_t>(manifest.missing_mesh_count);
        output_report->hidden_actor_count =
            static_cast<uint32_t>(manifest.hidden_actor_count);
        if (actor_capacity < output_report->required_actor_count) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_BUFFER_TOO_SMALL,
                              "character actor buffer is too small");
            return output_report->status;
        }
        std::vector<std::string> package_paths;
        package_paths.reserve(manifest.actors.size());
        for (const auto& actor : manifest.actors) {
            package_paths.push_back(path_to_utf8(actor.mesh_package));
            if (actor.object_name.size() >= HPVR_HP1_ACTOR_NAME_CAPACITY ||
                actor.qualified_class_name.size() >=
                    HPVR_HP1_CLASS_NAME_CAPACITY ||
                package_paths.back().size() >=
                    HPVR_HP1_PACKAGE_PATH_CAPACITY ||
                actor.mesh_object_path.size() >=
                    HPVR_HP1_ACTOR_NAME_CAPACITY) {
                set_profile_error(*output_report,
                                  HPVR_HP1_PROFILE_INVALID_PROFILE,
                                  "character manifest identity exceeds its C ABI capacity");
                return output_report->status;
            }
        }
        for (std::size_t index = 0; index < manifest.actors.size(); ++index) {
            const auto& source = manifest.actors[index];
            auto& target = output_actors[index];
            target = {};
            target.actor_slot_index =
                static_cast<uint32_t>(source.actor_slot_index);
            target.actor_reference = source.actor_reference;
            target.class_reference = source.class_reference;
            target.mesh_reference = source.mesh_reference;
            target.position_m[0] =
                source.location_unreal.y * meters_per_unreal_unit;
            target.position_m[1] =
                source.location_unreal.z * meters_per_unreal_unit;
            target.position_m[2] =
                -source.location_unreal.x * meters_per_unreal_unit;
            std::ranges::copy(source.rotation_units,
                              target.rotation_units);
            target.draw_scale = source.draw_scale;
            copy_text(target.object_name, source.object_name);
            copy_text(target.qualified_class_name,
                      source.qualified_class_name);
            copy_text(target.mesh_package_utf8, package_paths[index]);
            copy_text(target.mesh_object_path, source.mesh_object_path);
        }
        output_report->written_actor_count =
            output_report->required_actor_count;
        output_report->status = HPVR_HP1_PROFILE_OK;
        output_report->error[0] = '\0';
    } catch (const std::bad_alloc&) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_ALLOCATION_FAILURE,
                          "allocation failed inside the character-manifest bridge");
    } catch (...) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_INTERNAL_ERROR,
                          "exception contained inside the character-manifest bridge");
    }
    return output_report->status;
}

extern "C" uint32_t hpvr_hp1_load_player_start_utf8(
    const char* map_package_utf8,
    float meters_per_unreal_unit,
    uint32_t player_start_ordinal,
    hpvr_hp1_player_start_report* output_report) {
    if (output_report == nullptr) {
        return HPVR_HP1_PROFILE_INVALID_ARGUMENT;
    }
    *output_report = {};
    output_report->abi_version = HPVR_HP1_PLAYER_START_ABI_VERSION;
    if (map_package_utf8 == nullptr || *map_package_utf8 == '\0' ||
        !std::isfinite(meters_per_unreal_unit) ||
        meters_per_unreal_unit <= 0.0F) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_INVALID_ARGUMENT,
                          "invalid PlayerStart C ABI argument");
        return output_report->status;
    }

    try {
        const auto starts = hpvr::wand::inspect_hp1_player_starts(
            path_from_utf8(map_package_utf8));
        output_report->status = profile_status(starts.status);
        copy_text(output_report->error, starts.error);
        if (starts.status != hpvr::wand::Hp1ProfileStatus::ok) {
            return output_report->status;
        }
        if (starts.player_starts.size() >
            std::numeric_limits<uint32_t>::max()) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_INTERNAL_ERROR,
                              "PlayerStart count exceeds the C ABI range");
            return output_report->status;
        }
        output_report->available_player_start_count =
            static_cast<uint32_t>(starts.player_starts.size());
        if (player_start_ordinal >= starts.player_starts.size()) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_OBJECT_NOT_FOUND,
                              "PlayerStart ordinal is outside the map census");
            return output_report->status;
        }
        const auto& start = starts.player_starts[player_start_ordinal];
        if (start.actor_slot_index > std::numeric_limits<uint32_t>::max()) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_INTERNAL_ERROR,
                              "PlayerStart actor slot exceeds the C ABI range");
            return output_report->status;
        }
        const auto position = start.location_unreal;
        output_report->selected_ordinal = player_start_ordinal;
        output_report->actor_slot_index =
            static_cast<uint32_t>(start.actor_slot_index);
        output_report->actor_reference = start.actor_reference;
        output_report->position_m[0] = position.y * meters_per_unreal_unit;
        output_report->position_m[1] = position.z * meters_per_unreal_unit;
        output_report->position_m[2] = -position.x * meters_per_unreal_unit;
        std::ranges::copy(start.rotation_units,
                          output_report->rotation_units);
        output_report->location_serialized =
            start.location_serialized ? 1U : 0U;
        output_report->rotation_serialized =
            start.rotation_serialized ? 1U : 0U;
        copy_text(output_report->object_name, start.object_name);
        if (!std::ranges::all_of(
                output_report->position_m,
                [](float component) { return std::isfinite(component); })) {
            set_profile_error(*output_report,
                              HPVR_HP1_PROFILE_INVALID_PROFILE,
                              "scaled PlayerStart position is non-finite");
        }
    } catch (const std::bad_alloc&) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_ALLOCATION_FAILURE,
                          "allocation failed inside the PlayerStart bridge");
    } catch (...) {
        set_profile_error(*output_report,
                          HPVR_HP1_PROFILE_INTERNAL_ERROR,
                          "exception contained inside the PlayerStart bridge");
    }
    return output_report->status;
}

extern "C" int64_t hpvr_hp1_lesson_resampling_period_ns(
    float draw_time_seconds) {
    return hpvr::wand::hp1_lesson_resampling_period_ns(draw_time_seconds);
}
