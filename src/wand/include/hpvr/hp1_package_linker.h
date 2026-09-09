#pragma once

#include "hpvr/hp1_package_graph.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hpvr::wand {

enum class Hp1PackageLinkStatus {
    ok,
    graph_error,
    invalid_link_table,
    invalid_import_path,
    missing_target_package,
    missing_target_export,
    ambiguous_target_export,
    resource_limit,
};

enum class Hp1ImportTargetKind {
    package_root,
    package_group,
    export_object,
    native_object,
    native_module,
};

struct Hp1ResolvedImport {
    std::string source_package;
    std::int32_t source_reference{};
    std::string qualified_class_name;
    std::vector<std::string> source_object_path;
    std::string target_package;
    std::int32_t target_reference{};
    std::vector<std::string> target_object_path;
    Hp1ImportTargetKind target_kind{Hp1ImportTargetKind::export_object};
};

struct Hp1PackageLinkResult {
    Hp1PackageLinkStatus status{Hp1PackageLinkStatus::graph_error};
    std::string error;
    Hp1PackageGraph graph;
    std::vector<Hp1ResolvedImport> imports;
    std::size_t package_root_count{};
    std::size_t package_group_count{};
    std::size_t export_object_count{};
    std::size_t native_object_count{};
    std::size_t native_module_count{};
};

struct Hp1TexturedBspVertex {
    Hp1BspVector position_m{};
    Hp1BspVector normal{};
    std::array<float, 2> texture_uv{};
    std::array<float, 2> lightmap_uv{};
    std::uint32_t texture_layer{};
    std::uint32_t polygon_flags{};
    std::uint32_t has_lightmap{};
    std::uint32_t node_index{};
    std::uint32_t surface_index{};
};

struct Hp1TexturedBspScene {
    Hp1ProfileStatus status{Hp1ProfileStatus::invalid_profile};
    std::string error;
    std::vector<Hp1TexturedBspVertex> vertices;
    std::uint32_t texture_layer_width{};
    std::uint32_t texture_layer_height{};
    std::uint32_t texture_layer_count{};
    std::vector<std::string> texture_layer_names;
    // Tightly packed RGBA8 array layers. Pixels remain caller-owned and are
    // never written to disk by the clean-room loader.
    std::vector<std::uint8_t> texture_rgba8;
    std::uint32_t lightmap_width{};
    std::uint32_t lightmap_height{};
    // A single RGBA8 atlas. Rectangles include one-pixel replicated gutters.
    std::vector<std::uint8_t> lightmap_rgba8;
    std::size_t decoded_lightmap_count{};
    std::size_t lightmap_texel_count{};
    std::size_t lightmap_light_count{};
    std::size_t available_triangle_count{};
    std::size_t selected_triangle_count{};
    std::size_t omitted_triangle_count{};
    std::size_t decoded_texture_count{};
    std::size_t fallback_material_count{};
    std::size_t fallback_triangle_count{};
};

struct Hp1CharacterSkinOverride {
    std::size_t material_slot{};
    std::filesystem::path package;
    std::int32_t reference{};
};
struct Hp1CharacterActor {
    std::int32_t actor_reference{};
    std::int32_t class_reference{};
    std::size_t actor_slot_index{};
    std::string object_name;
    std::string qualified_class_name;
    Hp1BspVector location_unreal{};
    std::array<std::int32_t, 3> rotation_units{};
    float draw_scale{1.0F};
    std::filesystem::path mesh_package;
    std::string mesh_package_name;
    std::int32_t mesh_reference{};
    std::string mesh_object_path;
    std::vector<Hp1CharacterSkinOverride> skins;
};

struct Hp1CharacterManifest {
    Hp1ProfileStatus status{Hp1ProfileStatus::invalid_profile};
    std::string error;
    std::vector<Hp1CharacterActor> actors;
    std::size_t inspected_actor_count{};
    std::size_t excluded_actor_count{};
    std::size_t non_character_actor_count{};
    std::size_t missing_location_count{};
    std::size_t unresolved_class_count{};
    std::size_t missing_mesh_count{};
    std::size_t hidden_actor_count{};
};

// Resolves every import in the selected package closure. Exact matches require
// the target export's case-insensitive object path and qualified class identity.
// No export payload is decoded and no object is constructed.
[[nodiscard]] Hp1PackageLinkResult link_hp1_package_graph(
    const std::filesystem::path& data_root,
    const std::filesystem::path& entry_package);

// Resolves character actors through the validated package graph and UClass
// visual defaults. No UObject is constructed, no script is executed, and no
// mesh/texture payload is decoded by this manifest pass.
[[nodiscard]] Hp1CharacterManifest build_hp1_character_manifest(
    const std::filesystem::path& data_root,
    const std::filesystem::path& map_package,
    std::int32_t excluded_actor_reference = 0,
    const std::vector<Hp1ActorVisual>& additional_actors = {});

// Builds a bounded BSP preview with repeating normalized UVs and a uniform
// RGBA8 texture array. Direct Engine.Texture P8 materials are decoded from the
// user-owned dependency graph; unsupported dynamic/non-texture materials use
// layer zero's explicit diagnostic checkerboard.
[[nodiscard]] Hp1TexturedBspScene build_hp1_textured_bsp_scene(
    const std::filesystem::path& data_root,
    const std::filesystem::path& map_package,
    float meters_per_unreal_unit,
    std::uint32_t maximum_triangle_count,
    std::int32_t brush_model_reference = 0);

}  // namespace hpvr::wand
