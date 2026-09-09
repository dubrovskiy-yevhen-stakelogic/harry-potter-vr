#pragma once

#include "hpvr/wand_trajectory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hpvr::wand {

inline constexpr std::size_t kHp1PassMarkCount{10};
inline constexpr std::size_t kHp1NativeGesturePointLimit{1024};

enum class Hp1ProfileStatus {
    ok,
    io_error,
    invalid_package,
    unsupported_package,
    object_not_found,
    ambiguous_object,
    invalid_profile,
};

struct Hp1SpellProfile {
    Hp1ProfileStatus status{Hp1ProfileStatus::invalid_profile};
    std::string error;
    std::string gesture_name;
    std::string lesson_actor_name;
    std::vector<Vec2> template_points;
    std::vector<std::int32_t> segments;
    std::array<float, kHp1PassMarkCount> pass_marks{};
    std::size_t pass_mark_count{};
    float accuracy_radius{};
    float very_good_threshold{};
    float very_bad_threshold{};
    float default_draw_time_seconds{};
    float draw_time_seconds{};
    std::uint16_t base_package_version{};
    std::uint16_t lesson_package_version{};
};

struct Hp1PackageClassSummary {
    std::string qualified_class_name;
    std::size_t export_count{};
    std::uint64_t serialized_bytes{};
};

struct Hp1PackageSummary {
    Hp1ProfileStatus status{Hp1ProfileStatus::invalid_package};
    std::string error;
    std::uint16_t package_version{};
    std::uint16_t licensee_version{};
    std::uint64_t file_bytes{};
    std::size_t name_count{};
    std::size_t import_count{};
    std::size_t export_count{};
    std::uint64_t serialized_bytes{};
    std::vector<std::string> imported_packages;
    std::vector<Hp1PackageClassSummary> classes;
};

struct Hp1PackageImport {
    std::int32_t reference{};
    std::int32_t outer_reference{};
    std::string qualified_class_name;
    std::vector<std::string> object_path;
    bool root_package{};
};

struct Hp1PackageExport {
    std::int32_t reference{};
    std::int32_t outer_reference{};
    std::string qualified_class_name;
    std::vector<std::string> object_path;
    std::uint32_t object_flags{};
    std::uint64_t serialized_bytes{};
};

struct Hp1PackageLinkTable {
    Hp1ProfileStatus status{Hp1ProfileStatus::invalid_package};
    std::string error;
    std::uint16_t package_version{};
    std::uint16_t licensee_version{};
    std::vector<Hp1PackageImport> imports;
    std::vector<Hp1PackageExport> exports;
};

// Bounded caller-owned payload for clean-room content adapters. No file writes.
struct Hp1ExportPayload {
    Hp1ProfileStatus status{Hp1ProfileStatus::invalid_package};
    std::string error;
    std::vector<std::uint8_t> bytes;
};
[[nodiscard]] Hp1ExportPayload load_hp1_export_payload(
    const std::filesystem::path& package, std::int32_t reference);

struct Hp1LevelHandles {
    Hp1ProfileStatus status{Hp1ProfileStatus::invalid_profile};
    std::string error;
    std::uint16_t package_version{};
    std::int32_t level_reference{};
    std::int32_t world_model_reference{};
    std::vector<std::int32_t> actor_references;
    std::size_t null_actor_count{};
};

struct Hp1BspVector {
    float x{};
    float y{};
    float z{};
};

struct Hp1Quaternion {
    float x{};
    float y{};
    float z{};
    float w{1.0F};
};

struct Hp1ModelCensus {
    Hp1ProfileStatus status{Hp1ProfileStatus::invalid_profile};
    std::string error;
    std::uint16_t package_version{};
    std::int32_t model_reference{};
    std::int32_t polys_reference{};
    std::size_t vector_count{};
    std::size_t point_count{};
    std::size_t node_count{};
    std::size_t surface_count{};
    std::size_t vertex_count{};
    std::size_t shared_side_count{};
    std::size_t zone_count{};
    std::size_t light_map_count{};
    std::size_t light_bit_bytes{};
    std::size_t bound_count{};
    std::size_t leaf_hull_count{};
    std::size_t leaf_count{};
    std::size_t light_count{};
    std::size_t null_light_count{};
    bool root_outside{};
    bool linked{};
};

struct Hp1SkeletalMeshCensus {
    Hp1ProfileStatus status{Hp1ProfileStatus::invalid_profile};
    std::string error;
    std::uint16_t package_version{};
    std::int32_t mesh_reference{};
    std::string object_name;
    std::size_t packed_vertex_count{};
    std::size_t legacy_triangle_count{};
    std::size_t animation_sequence_count{};
    std::size_t vertex_connect_count{};
    std::size_t texture_count{};
    std::vector<std::int32_t> texture_references;
    std::size_t bounding_box_count{};
    std::size_t bounding_sphere_count{};
    std::int32_t vertex_count{};
    std::int32_t frame_count{};
    Hp1BspVector mesh_scale{};
    Hp1BspVector mesh_origin{};
    std::array<std::int32_t, 3> rotation_origin{};
    std::size_t collapse_point_count{};
    std::size_t face_level_count{};
    std::size_t face_count{};
    std::size_t collapse_wedge_count{};
    std::size_t lod_wedge_count{};
    std::size_t material_count{};
    std::vector<std::int32_t> material_texture_indices;
    std::size_t special_face_count{};
    std::int32_t model_vertex_count{};
    std::int32_t special_vertex_count{};
    std::size_t remap_vertex_count{};
    std::size_t skeletal_wedge_count{};
    std::size_t point_count{};
    std::size_t bone_count{};
    std::size_t bone_index_count{};
    std::size_t bone_weight_count{};
    std::size_t local_point_count{};
    std::int32_t skeletal_depth{};
    std::int32_t animation_reference{};
};

struct Hp1SkeletalMeshVertex {
    Hp1BspVector position_m{};
    std::array<float, 2> texture_uv{};
    std::uint32_t material_index{};
    std::uint32_t polygon_flags{};
    std::uint32_t face_index{};
    std::uint32_t point_index{};
};

struct Hp1SkeletalTriangleMesh {
    Hp1ProfileStatus status{Hp1ProfileStatus::invalid_profile};
    std::string error;
    Hp1SkeletalMeshCensus census;
    float meters_per_unreal_unit{};
    std::vector<Hp1SkeletalMeshVertex> vertices;
    Hp1BspVector bounds_min_m{};
    Hp1BspVector bounds_max_m{};
};

struct Hp1SkeletalBone {
    std::string name;
    std::uint32_t flags{};
    Hp1Quaternion orientation{};
    Hp1BspVector position{};
    float length{};
    Hp1BspVector size{};
    std::int32_t child_count{};
    std::int32_t parent_index{};
};

struct Hp1SkeletalBoneWeightSpan {
    std::uint16_t weight_offset{};
    std::uint16_t weight_count{};
    std::uint16_t detail_count_a{};
    std::uint16_t detail_count_b{};
};

struct Hp1SkeletalBoneWeight {
    std::uint16_t point_index{};
    std::uint16_t encoded_weight{};
};

struct Hp1SkeletalSkin {
    Hp1ProfileStatus status{Hp1ProfileStatus::invalid_profile};
    std::string error;
    Hp1SkeletalMeshCensus census;
    std::vector<Hp1BspVector> points;
    std::vector<Hp1SkeletalBone> bones;
    std::vector<Hp1SkeletalBoneWeightSpan> bone_weight_spans;
    std::vector<Hp1SkeletalBoneWeight> bone_weights;
    std::vector<Hp1BspVector> local_points;
};

struct Hp1AnimationNamedBone {
    std::string name;
    std::uint32_t flags{};
    std::int32_t parent_index{};
};

struct Hp1AnimationTrack {
    std::uint32_t flags{};
    std::size_t orientation_offset{};
    std::size_t orientation_count{};
    std::size_t position_offset{};
    std::size_t position_count{};
    std::size_t time_offset{};
    std::size_t time_count{};
    float position_scale{};
    float time_scale{};
};

struct Hp1AnimationMove {
    Hp1BspVector root_speed{};
    float track_time{};
    std::int32_t start_bone{};
    std::uint32_t flags{};
    std::vector<std::int32_t> bone_indices;
    std::vector<Hp1AnimationTrack> tracks;
};

struct Hp1AnimationSequence {
    std::string name;
    std::string group;
    std::int32_t start_frame{};
    std::int32_t frame_count{};
    float rate{};
    std::size_t notify_count{};
};

struct Hp1Animation {
    Hp1ProfileStatus status{Hp1ProfileStatus::invalid_profile};
    std::string error;
    std::uint16_t package_version{};
    std::int32_t animation_reference{};
    std::string object_name;
    std::vector<Hp1AnimationNamedBone> bones;
    std::vector<Hp1AnimationMove> moves;
    std::vector<Hp1AnimationSequence> sequences;
    std::vector<std::array<std::int16_t, 3>> compressed_orientation_keys;
    std::vector<std::array<std::int16_t, 3>> compressed_position_keys;
    std::vector<std::uint8_t> compressed_time_keys;
    std::size_t orientation_key_count{};
    std::size_t position_key_count{};
    std::size_t time_key_count{};
};

struct Hp1SkeletalPose {
    Hp1ProfileStatus status{Hp1ProfileStatus::invalid_profile};
    std::string error;
    std::size_t sequence_index{};
    float sample_time_seconds{};
    std::vector<Hp1BspVector> points;
};

struct Hp1PlayerStart {
    std::int32_t actor_reference{};
    std::size_t actor_slot_index{};
    std::string object_name;
    Hp1BspVector location_unreal{};
    std::array<std::int32_t, 3> rotation_units{};
    bool location_serialized{};
    bool rotation_serialized{};
};

struct Hp1PlayerStartCensus {
    Hp1ProfileStatus status{Hp1ProfileStatus::invalid_profile};
    std::string error;
    std::uint16_t package_version{};
    std::vector<Hp1PlayerStart> player_starts;
};

struct Hp1ClassDefaultProperty {
    std::string name;
    std::uint8_t kind{};
    std::string structure_name;
    std::int64_t array_index{-1};
    bool boolean_value{};
    bool boolean_value_serialized{};
    std::vector<std::uint8_t> value;
    std::string text_value;
    bool text_value_serialized{};
    std::int32_t object_reference{};
    std::vector<std::string> object_path;
    bool object_reference_serialized{};
};

struct Hp1ActorVisual {
    std::int32_t actor_reference{};
    std::int32_t class_reference{};
    std::size_t actor_slot_index{};
    std::string object_name;
    std::string qualified_class_name;
    Hp1BspVector location_unreal{};
    std::array<std::int32_t, 3> rotation_units{};
    float draw_scale{1.0F};
    std::int32_t mesh_reference{};
    std::vector<std::int32_t> skin_references;
    std::string animation_sequence;
    std::string tag;
    std::string event;
    std::string initial_state;
    std::int32_t ambient_sound_reference{};
    float collision_radius{};
    float collision_height{};
    std::uint8_t sound_volume{255};
    std::uint8_t sound_radius{};
    std::uint8_t sound_pitch{64};
    std::uint8_t light_brightness{};
    std::uint8_t light_hue{};
    std::uint8_t light_saturation{255};
    std::uint8_t light_radius{};
    std::uint8_t light_type{};
    std::uint8_t light_effect{};
    std::uint8_t draw_type{};
    bool location_serialized{};
    bool rotation_serialized{};
    bool draw_scale_serialized{};
    bool mesh_serialized{};
    bool animation_sequence_serialized{};
    bool draw_type_serialized{};
    bool hidden{};
    bool hidden_serialized{};
    bool tag_serialized{};
    bool event_serialized{};
    bool initial_state_serialized{};
    bool ambient_sound_serialized{};
    bool collision_radius_serialized{};
    bool collision_height_serialized{};
    bool collide_actors{};
    bool collide_actors_serialized{};
    bool block_actors{};
    bool block_actors_serialized{};
    bool block_players{};
    bool block_players_serialized{};
    bool sound_volume_serialized{};
    bool sound_radius_serialized{};
    bool sound_pitch_serialized{};
    bool light_brightness_serialized{};
    bool light_hue_serialized{};
    bool light_saturation_serialized{};
    bool light_radius_serialized{};
    bool light_type_serialized{};
    bool light_effect_serialized{};
    std::vector<Hp1ClassDefaultProperty> serialized_properties;
    std::size_t property_count{};
};

struct Hp1ActorVisualCensus {
    Hp1ProfileStatus status{Hp1ProfileStatus::invalid_profile};
    std::string error;
    std::uint16_t package_version{};
    std::vector<Hp1ActorVisual> actors;
};

struct Hp1ClassVisualDefaults {
    Hp1ProfileStatus status{Hp1ProfileStatus::invalid_profile};
    std::string error;
    std::uint16_t package_version{};
    std::int32_t class_reference{};
    std::int32_t super_reference{};
    std::string object_name;
    std::vector<std::string> super_object_path;
    float draw_scale{1.0F};
    std::int32_t mesh_reference{};
    std::vector<std::string> mesh_object_path;
    std::vector<std::int32_t> skin_references;
    std::string animation_sequence;
    float base_eye_height{};
    float collision_height{};
    std::uint8_t draw_type{};
    bool draw_scale_serialized{};
    bool mesh_serialized{};
    bool animation_sequence_serialized{};
    bool base_eye_height_serialized{};
    bool collision_height_serialized{};
    bool draw_type_serialized{};
    bool hidden{};
    bool hidden_serialized{};
    std::vector<Hp1ClassDefaultProperty> serialized_properties;
    std::size_t property_count{};
};

struct Hp1TextureMipInfo {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint8_t width_bits{};
    std::uint8_t height_bits{};
    std::size_t indexed_bytes{};
};

struct Hp1P8Texture {
    Hp1ProfileStatus status{Hp1ProfileStatus::invalid_profile};
    std::string error;
    std::uint16_t package_version{};
    std::int32_t texture_reference{};
    std::int32_t palette_reference{};
    std::string object_name;
    std::uint8_t format{};
    bool format_serialized{};
    bool compressed_mips_serialized{};
    std::vector<Hp1TextureMipInfo> mips;
    // RGBA8 pixels for mip zero only. Proprietary pixels stay in caller-owned
    // memory and are never written by this API.
    std::vector<std::uint8_t> rgba8;
};

struct Hp1SoundAsset {
    std::int32_t sound_reference{};
    std::string object_name;
    std::size_t serialized_bytes{};
    std::size_t wave_bytes{};
    std::array<std::uint8_t, 24> payload_prefix{};
    std::size_t payload_prefix_bytes{};
};

struct Hp1SoundCensus {
    Hp1ProfileStatus status{Hp1ProfileStatus::invalid_profile};
    std::string error;
    std::uint16_t package_version{};
    std::vector<Hp1SoundAsset> sounds;
};

struct Hp1PcmSound {
    Hp1ProfileStatus status{Hp1ProfileStatus::invalid_profile};
    std::string error;
    std::uint16_t package_version{};
    std::int32_t sound_reference{};
    std::string object_name;
    std::uint32_t sample_rate{};
    std::uint16_t channel_count{};
    std::vector<std::int16_t> samples;
};

struct Hp1MpegSound {
    Hp1ProfileStatus status{Hp1ProfileStatus::invalid_profile};
    std::string error;
    std::uint16_t package_version{};
    std::int32_t sound_reference{};
    std::string object_name;
    std::uint32_t sample_rate{};
    std::uint16_t channel_count{};
    std::vector<std::uint8_t> encoded_bytes;
};

struct Hp1BspNode {
    std::array<float, 4> plane{};
    std::uint64_t zone_mask{};
    std::int32_t vertex_pool_index{};
    std::int32_t surface_index{};
    std::int32_t front_node_index{};
    std::int32_t back_node_index{};
    std::int32_t collision_bound_index{};
    std::array<std::uint8_t, 2> zone_indices{};
    std::uint8_t vertex_count{};
    std::uint8_t flags{};
    std::array<std::int32_t, 2> leaf_indices{};
};

struct Hp1BspSurface {
    std::int32_t texture_reference{};
    std::uint32_t polygon_flags{};
    std::int32_t base_point_index{};
    std::int32_t normal_vector_index{};
    std::int32_t texture_u_vector_index{};
    std::int32_t texture_v_vector_index{};
    std::int32_t light_map_index{};
    std::int16_t pan_u{};
    std::int16_t pan_v{};
    std::int32_t actor_reference{};
};

struct Hp1BspVertex {
    std::int32_t point_index{};
    std::int32_t side_index{};
};

struct Hp1LightMapIndex {
    std::int32_t data_offset{};
    Hp1BspVector pan{};
    std::int32_t u_clamp{};
    std::int32_t v_clamp{};
    float u_scale{};
    float v_scale{};
    std::int32_t light_actor_index{};
};

struct Hp1BspTopology {
    Hp1ProfileStatus status{Hp1ProfileStatus::invalid_profile};
    std::string error;
    std::uint16_t package_version{};
    std::int32_t model_reference{};
    std::vector<Hp1BspVector> vectors;
    std::vector<Hp1BspVector> points;
    std::vector<Hp1BspNode> nodes;
    std::vector<Hp1BspSurface> surfaces;
    std::vector<Hp1LightMapIndex> light_maps;
    std::vector<std::uint8_t> light_bits;
    // Model.Lights references. light_actor_index starts a zero-ended span.
    std::vector<std::int32_t> light_references;
    // Contains only node-referenced spans. Node vertex_pool_index values are
    // remapped to this compact validated collection.
    std::vector<Hp1BspVertex> vertices;
    std::size_t shared_side_count{};
    std::size_t zone_count{};
    std::size_t bound_count{};
    std::size_t leaf_count{};
};

struct Hp1BspTriangle {
    std::array<Hp1BspVector, 3> positions_m{};
    // Legacy Unreal texture coordinates in texel units. They are derived from
    // the unscaled source point, surface base, texture vectors, and pan.
    std::array<std::array<float, 2>, 3> texel_uv{};
    Hp1BspVector normal{};
    std::uint32_t node_index{};
    std::uint32_t surface_index{};
};

struct Hp1BspTriangleMesh {
    Hp1ProfileStatus status{Hp1ProfileStatus::invalid_profile};
    std::string error;
    float meters_per_unreal_unit{};
    std::vector<Hp1BspTriangle> triangles;
    std::size_t source_polygon_count{};
    std::size_t short_polygon_count{};
    std::size_t degenerate_triangle_count{};
    std::size_t reversed_winding_count{};
};

// Reads and validates only the UE1 package header and name/import/export tables.
// Payload bytes are bounds-checked and counted but never decoded or written.
[[nodiscard]] Hp1PackageSummary inspect_hp1_package(
    const std::filesystem::path& package_path);

// Returns identity metadata only. Payload bytes remain unread except for the
// bounds validation performed by the shared package-table parser.
[[nodiscard]] Hp1PackageLinkTable inspect_hp1_package_link_table(
    const std::filesystem::path& package_path);

// Decodes only the top-level Engine.Level actor-reference array, its FURL
// framing, and the following Engine.Model reference. No actors, models,
// properties, scripts, or geometry are constructed.
[[nodiscard]] Hp1LevelHandles inspect_hp1_level_handles(
    const std::filesystem::path& map_package);

// Selects only direct Engine.PlayerStart exports referenced by the Level actor
// array and decodes their serialized Location/Rotation tags. No actor is
// constructed and class inheritance or default properties are not inferred.
[[nodiscard]] Hp1PlayerStartCensus inspect_hp1_player_starts(
    const std::filesystem::path& map_package);

// Decodes only instance-level visual placement overrides for local actors in
// the validated Level array. Class defaults, inheritance, scripts, meshes,
// skins, and animation payloads are not followed or constructed.
[[nodiscard]] Hp1ActorVisualCensus inspect_hp1_actor_visuals(
    const std::filesystem::path& map_package);

// Decodes only one local UClass export header and its serialized visual default
// properties. Bytecode is never interpreted; this gate accepts only classes
// with an empty script body. Superclass and asset references remain identities
// for the caller to resolve through the audited package linker.
[[nodiscard]] Hp1ClassVisualDefaults inspect_hp1_class_visual_defaults(
    const std::filesystem::path& package_path,
    std::int32_t class_reference);

// Decodes one local direct Engine.Texture export and its local Engine.Palette.
// Gate B9 supports only the legacy P8 layout observed in owned HP1 packages.
// No image or package data is written.
[[nodiscard]] Hp1P8Texture load_hp1_p8_texture(
    const std::filesystem::path& texture_package,
    std::int32_t texture_reference, bool palette_zero_transparent = false);

// Enumerates direct Engine.Sound exports and validates the embedded RIFF/WAVE
// boundaries without exporting any user-owned audio to disk.
[[nodiscard]] Hp1SoundCensus inspect_hp1_sound_assets(
    const std::filesystem::path& sound_package);

// Decodes one direct Engine.Sound PCM WAVE payload into caller-owned memory.
// Both shipped 8-bit unsigned and 16-bit signed PCM are normalized to int16.
[[nodiscard]] Hp1PcmSound load_hp1_pcm_sound(
    const std::filesystem::path& sound_package,
    std::int32_t sound_reference);

[[nodiscard]] Hp1MpegSound load_hp1_mpeg_sound(
    const std::filesystem::path& sound_package,
    std::int32_t sound_reference);

// Follows the top-level Level's world-model reference and decodes only the
// bounded Engine.Model collection framing. Geometry, materials, polygons,
// scripts, and UObject instances are never constructed or retained.
[[nodiscard]] Hp1ModelCensus inspect_hp1_model_census(
    const std::filesystem::path& map_package);

// Decodes the bounded UE1 UPrimitive -> UMesh -> ULodMesh -> USkeletalMesh
// framing of one local Engine.SkeletalMesh export. Animation payloads are not
// followed, UObject instances are not constructed, and source data is never
// written. Every lazy-array end offset, reference, and exposed topology index
// is validated before the census is accepted.
[[nodiscard]] Hp1SkeletalMeshCensus inspect_hp1_skeletal_mesh_census(
    const std::filesystem::path& mesh_package,
    std::int32_t mesh_reference);

// Builds the validated bind-pose triangle stream directly from the owned UE1
// package. It reproduces the legacy HP1/UE Viewer handedness conversion used
// by the accepted B10 PSK oracle but performs no export and writes no files.
[[nodiscard]] Hp1SkeletalTriangleMesh build_hp1_skeletal_triangle_mesh(
    const std::filesystem::path& mesh_package,
    std::int32_t mesh_reference,
    float meters_per_unreal_unit);

// Retains the exact bounded reference skeleton, per-bone influence spans, and
// matching bone-local points needed for CPU skinning. It does not evaluate an
// animation or mutate the package.
[[nodiscard]] Hp1SkeletalSkin load_hp1_skeletal_skin(
    const std::filesystem::path& mesh_package,
    std::int32_t mesh_reference);

// Decodes HP1's bounded version-76 Engine.Animation hierarchy behind one direct
// local export. Track headers reference three shared compressed-key arrays;
// no UObject or script execution occurs and source data is never written.
[[nodiscard]] Hp1Animation load_hp1_animation(
    const std::filesystem::path& animation_package,
    std::int32_t animation_reference);

// Independent equivalents of the shipped six-byte FAnimVec decoders. Position
// scale is authored per track. Quaternion W is reconstructed non-negative,
// matching the sign-canonicalized encoder used by HP1.
[[nodiscard]] Hp1Quaternion decode_hp1_animation_orientation_key(
    std::array<std::int16_t, 3> key) noexcept;
[[nodiscard]] Hp1BspVector decode_hp1_animation_position_key(
    std::array<std::int16_t, 3> key,
    float track_scale) noexcept;
[[nodiscard]] float decode_hp1_animation_time_key(
    std::uint8_t key,
    float track_scale) noexcept;

// Samples one looping sequence and skins every source point in original mesh
// coordinates. The function validates skeleton identity, bone-to-track maps,
// key ranges, and hierarchy ordering before exposing a pose.
[[nodiscard]] Hp1SkeletalPose sample_hp1_skeletal_animation(
    const Hp1SkeletalSkin& skin,
    const Hp1Animation& animation,
    std::size_t sequence_index,
    float elapsed_seconds,
    bool looping = true,
    bool stabilize_root_height = false);

// Decodes the minimal immutable BSP topology arrays and validates every
// exposed cross-array index. Materials, polygons, light payloads, collision
// construction, actors, scripts, and render meshes remain deferred.
[[nodiscard]] Hp1BspTopology load_hp1_bsp_topology(
    const std::filesystem::path& map_package);

// Builds a CPU-only CCW triangle stream in OpenXR axes: Unreal +Y becomes
// OpenXR +X, Unreal +Z becomes OpenXR +Y, and Unreal +X becomes OpenXR -Z.
// World scale remains an explicit caller-owned input until calibrated.
[[nodiscard]] Hp1BspTriangleMesh build_hp1_bsp_triangle_mesh(
    const Hp1BspTopology& topology,
    float meters_per_unreal_unit);

[[nodiscard]] Hp1BspTopology load_hp1_brush_topology(
    const std::filesystem::path& map_package, std::int32_t model_reference);

// Reads only package metadata, one named Engine.Gesture export, the compiled
// SpellLearnTrigger numeric defaults, and the matching lesson actor overrides.
// The source packages remain external and are never modified.
[[nodiscard]] Hp1SpellProfile load_hp1_spell_profile(
    const std::filesystem::path& base_package,
    const std::filesystem::path& lesson_package,
    std::string_view gesture_name,
    std::string_view spell_class_name);

enum class Hp1GestureScoreStatus {
    ok,
    invalid_accuracy,
    invalid_input,
    invalid_template,
};

struct Hp1GestureScore {
    Hp1GestureScoreStatus status{Hp1GestureScoreStatus::invalid_input};
    float score{};
    std::size_t input_point_count{};
    std::size_t unique_input_point_count{};
    std::size_t dense_template_point_count{};
};

// Clean-room equivalent of the HP1 Engine.Gesture coverage score. A Z value of
// exactly -1 is the shipped unused-slot sentinel. Segments are retained as
// package metadata but are deliberately not an input to the shipped scorer.
[[nodiscard]] Hp1GestureScore compare_hp1_gesture(
    std::span<const Vec3> drawn_points,
    std::span<const Vec2> template_points,
    float accuracy_radius);

// VR projection produces an already-compacted XY trajectory with no unused
// legacy slots. This convenience entry point submits each point with Z=0.
[[nodiscard]] Hp1GestureScore compare_hp1_projected_gesture(
    std::span<const Vec2> drawn_points,
    std::span<const Vec2> template_points,
    float accuracy_radius);

// The shipped lesson writes into 500 temporal slots over DrawTime seconds.
[[nodiscard]] std::int64_t hp1_lesson_resampling_period_ns(
    float draw_time_seconds) noexcept;

}  // namespace hpvr::wand
