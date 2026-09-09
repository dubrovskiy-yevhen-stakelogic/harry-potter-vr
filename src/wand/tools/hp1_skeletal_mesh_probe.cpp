#include "hpvr/hp1_gesture.h"

#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

using hpvr::wand::Hp1BspVector;
using hpvr::wand::Hp1Quaternion;

Hp1Quaternion normalized(Hp1Quaternion value) {
    const float length = std::sqrt(
        value.x * value.x + value.y * value.y +
        value.z * value.z + value.w * value.w);
    return {
        value.x / length, value.y / length, value.z / length, value.w / length};
}

Hp1Quaternion multiplied(Hp1Quaternion left, Hp1Quaternion right) {
    return {
        left.w * right.x + left.x * right.w +
            left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z +
            left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y -
            left.y * right.x + left.z * right.w,
        left.w * right.w - left.x * right.x -
            left.y * right.y - left.z * right.z,
    };
}

Hp1BspVector rotated(
    Hp1Quaternion rotation,
    Hp1BspVector point,
    bool inverse) {
    rotation = normalized(rotation);
    if (inverse) {
        rotation.x = -rotation.x;
        rotation.y = -rotation.y;
        rotation.z = -rotation.z;
    }
    const Hp1BspVector axis{rotation.x, rotation.y, rotation.z};
    const Hp1BspVector cross{
        axis.y * point.z - axis.z * point.y,
        axis.z * point.x - axis.x * point.z,
        axis.x * point.y - axis.y * point.x,
    };
    const Hp1BspVector second_cross{
        axis.y * cross.z - axis.z * cross.y,
        axis.z * cross.x - axis.x * cross.z,
        axis.x * cross.y - axis.y * cross.x,
    };
    return {
        point.x + 2.0F * (rotation.w * cross.x + second_cross.x),
        point.y + 2.0F * (rotation.w * cross.y + second_cross.y),
        point.z + 2.0F * (rotation.w * cross.z + second_cross.z),
    };
}

struct BoneTransform {
    Hp1Quaternion orientation;
    Hp1BspVector position;
};

void report_bind_reconstruction(
    const hpvr::wand::Hp1SkeletalSkin& skin,
    bool inverse_rotation,
    bool reverse_multiply,
    bool hierarchical) {
    std::vector<BoneTransform> transforms;
    transforms.reserve(skin.bones.size());
    for (std::size_t index = 0; index < skin.bones.size(); ++index) {
        const auto& bone = skin.bones[index];
        BoneTransform transform{normalized(bone.orientation), bone.position};
        if (hierarchical && bone.parent_index >= 0 &&
            static_cast<std::size_t>(bone.parent_index) != index) {
            const auto parent_index =
                static_cast<std::size_t>(bone.parent_index);
            if (parent_index >= transforms.size()) {
                std::cout << "bind_reconstruction_status=unsupported"
                          << " reason=parent_order mode="
                          << inverse_rotation << reverse_multiply
                          << hierarchical << '\n';
                return;
            }
            const auto& parent = transforms[parent_index];
            const auto relative_position =
                rotated(parent.orientation, bone.position, inverse_rotation);
            transform.position = {
                parent.position.x + relative_position.x,
                parent.position.y + relative_position.y,
                parent.position.z + relative_position.z,
            };
            transform.orientation = normalized(
                reverse_multiply
                    ? multiplied(transform.orientation, parent.orientation)
                    : multiplied(parent.orientation, transform.orientation));
        }
        transforms.push_back(transform);
    }

    std::vector<Hp1BspVector> reconstructed(skin.points.size());
    for (std::size_t bone_index = 0;
         bone_index < skin.bone_weight_spans.size();
         ++bone_index) {
        const auto& span = skin.bone_weight_spans[bone_index];
        const auto& transform = transforms[bone_index];
        for (std::size_t offset = 0; offset < span.weight_count; ++offset) {
            const auto weight_index = span.weight_offset + offset;
            const auto& influence = skin.bone_weights[weight_index];
            const auto local = rotated(
                transform.orientation,
                skin.local_points[weight_index],
                inverse_rotation);
            const float weight =
                static_cast<float>(influence.encoded_weight) / 65535.0F;
            auto& target = reconstructed[influence.point_index];
            target.x += (local.x + transform.position.x) * weight;
            target.y += (local.y + transform.position.y) * weight;
            target.z += (local.z + transform.position.z) * weight;
        }
    }
    double squared_error = 0.0;
    float maximum_error = 0.0F;
    for (std::size_t index = 0; index < skin.points.size(); ++index) {
        const auto& actual = skin.points[index];
        const auto& decoded = reconstructed[index];
        const float x = decoded.x - actual.x;
        const float y = decoded.y - actual.y;
        const float z = decoded.z - actual.z;
        const float error = std::sqrt(x * x + y * y + z * z);
        squared_error += static_cast<double>(error) * error;
        maximum_error = std::max(maximum_error, error);
    }
    const float rms_error = std::sqrt(
        static_cast<float>(squared_error /
                           static_cast<double>(skin.points.size())));
    std::cout << "bind_reconstruction_status=ok"
              << " inverse_rotation=" << inverse_rotation
              << " reverse_multiply=" << reverse_multiply
              << " hierarchical=" << hierarchical
              << " rms_error=" << rms_error
              << " max_error=" << maximum_error << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 && argc != 6) {
        std::cerr << "usage: hpvr_hp1_skeletal_mesh_probe "
                     "<mesh-package> <export-reference> "
                     "[<animation-package> <animation-reference> "
                     "<sequence-index>]\n";
        return EXIT_FAILURE;
    }
    std::int32_t reference{};
    try {
        reference = std::stoi(argv[2]);
    } catch (const std::exception&) {
        std::cerr << "export-reference must be a signed decimal integer\n";
        return EXIT_FAILURE;
    }
    const auto result = hpvr::wand::inspect_hp1_skeletal_mesh_census(
        std::filesystem::path(argv[1]), reference);
    if (result.status != hpvr::wand::Hp1ProfileStatus::ok) {
        std::cerr << "skeletal_mesh_status="
                  << static_cast<int>(result.status)
                  << " error=" << result.error << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "skeletal_mesh_status=ok"
              << " package_version=" << result.package_version
              << " mesh_reference=" << result.mesh_reference
              << " object=" << result.object_name
              << " packed_vertices=" << result.packed_vertex_count
              << " legacy_triangles=" << result.legacy_triangle_count
              << " animation_sequences="
              << result.animation_sequence_count
              << " vertex_connects=" << result.vertex_connect_count
              << " textures=" << result.texture_count
              << " bounding_boxes=" << result.bounding_box_count
              << " bounding_spheres=" << result.bounding_sphere_count
              << " vertex_count=" << result.vertex_count
              << " frame_count=" << result.frame_count
              << " mesh_scale=" << result.mesh_scale.x << ','
              << result.mesh_scale.y << ',' << result.mesh_scale.z
              << " mesh_origin=" << result.mesh_origin.x << ','
              << result.mesh_origin.y << ',' << result.mesh_origin.z
              << " rotation_origin=" << result.rotation_origin[0] << ','
              << result.rotation_origin[1] << ','
              << result.rotation_origin[2]
              << " collapse_points=" << result.collapse_point_count
              << " face_levels=" << result.face_level_count
              << " faces=" << result.face_count
              << " collapse_wedges=" << result.collapse_wedge_count
              << " lod_wedges=" << result.lod_wedge_count
              << " materials=" << result.material_count
              << " special_faces=" << result.special_face_count
              << " model_vertices=" << result.model_vertex_count
              << " special_vertices=" << result.special_vertex_count
              << " remap_vertices=" << result.remap_vertex_count
              << " skeletal_wedges=" << result.skeletal_wedge_count
              << " points=" << result.point_count
              << " bones=" << result.bone_count
              << " bone_indices=" << result.bone_index_count
              << " bone_weights=" << result.bone_weight_count
              << " local_points=" << result.local_point_count
              << " skeletal_depth=" << result.skeletal_depth
              << " animation_reference=" << result.animation_reference
              << '\n';
    std::cout << "texture_references=";
    for (const auto texture_reference : result.texture_references) {
        std::cout << texture_reference << ',';
    }
    std::cout << " material_texture_indices=";
    for (const auto index : result.material_texture_indices) {
        std::cout << index << ',';
    }
    std::cout << '\n';
    for (std::size_t material = 0;
         material < result.material_texture_indices.size();
         ++material) {
        const auto texture_slot =
            static_cast<std::size_t>(result.material_texture_indices[material]);
        const auto texture_reference = result.texture_references[texture_slot];
        const auto texture = hpvr::wand::load_hp1_p8_texture(
            std::filesystem::path(argv[1]), texture_reference);
        if (texture.status != hpvr::wand::Hp1ProfileStatus::ok ||
            texture.mips.empty()) {
            std::cerr << "material=" << material
                      << " texture_status=" << static_cast<int>(texture.status)
                      << " error=" << texture.error << '\n';
            return EXIT_FAILURE;
        }
        std::cout << "material=" << material
                  << " texture_reference=" << texture_reference
                  << " texture=" << texture.object_name
                  << " size=" << texture.mips.front().width << 'x'
                  << texture.mips.front().height
                  << " rgba_bytes=" << texture.rgba8.size() << '\n';
    }
    const auto triangles = hpvr::wand::build_hp1_skeletal_triangle_mesh(
        std::filesystem::path(argv[1]), reference, 0.02F);
    if (triangles.status != hpvr::wand::Hp1ProfileStatus::ok) {
        std::cerr << "triangle_mesh_status="
                  << static_cast<int>(triangles.status)
                  << " error=" << triangles.error << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "triangle_mesh_status=ok"
              << " vertices=" << triangles.vertices.size()
              << " triangles=" << triangles.vertices.size() / 3U
              << " bounds_min_m=" << triangles.bounds_min_m.x << ','
              << triangles.bounds_min_m.y << ','
              << triangles.bounds_min_m.z
              << " bounds_max_m=" << triangles.bounds_max_m.x << ','
              << triangles.bounds_max_m.y << ','
              << triangles.bounds_max_m.z << '\n';

    const auto skin = hpvr::wand::load_hp1_skeletal_skin(
        std::filesystem::path(argv[1]), reference);
    if (skin.status != hpvr::wand::Hp1ProfileStatus::ok) {
        std::cerr << "skeletal_skin_status="
                  << static_cast<int>(skin.status)
                  << " error=" << skin.error << '\n';
        return EXIT_FAILURE;
    }
    std::vector<std::uint32_t> point_weight_sums(skin.points.size());
    std::vector<std::uint32_t> point_influence_counts(skin.points.size());
    std::size_t referenced_weight_count = 0;
    for (const auto& span : skin.bone_weight_spans) {
        referenced_weight_count += span.weight_count;
        for (std::size_t index = 0; index < span.weight_count; ++index) {
            const auto& weight =
                skin.bone_weights[span.weight_offset + index];
            point_weight_sums[weight.point_index] += weight.encoded_weight;
            ++point_influence_counts[weight.point_index];
        }
    }
    std::size_t influenced_point_count = 0;
    std::uint32_t minimum_weight_sum =
        std::numeric_limits<std::uint32_t>::max();
    std::uint32_t maximum_weight_sum = 0;
    std::uint32_t maximum_influence_count = 0;
    for (std::size_t point = 0; point < skin.points.size(); ++point) {
        if (point_influence_counts[point] == 0) {
            continue;
        }
        ++influenced_point_count;
        minimum_weight_sum =
            std::min(minimum_weight_sum, point_weight_sums[point]);
        maximum_weight_sum =
            std::max(maximum_weight_sum, point_weight_sums[point]);
        maximum_influence_count = std::max(
            maximum_influence_count, point_influence_counts[point]);
    }
    if (influenced_point_count == 0) {
        minimum_weight_sum = 0;
    }
    std::cout << "skeletal_skin_status=ok"
              << " bones=" << skin.bones.size()
              << " spans=" << skin.bone_weight_spans.size()
              << " weights=" << skin.bone_weights.size()
              << " referenced_weights=" << referenced_weight_count
              << " local_points=" << skin.local_points.size()
              << " influenced_points=" << influenced_point_count
              << " uninfluenced_points="
              << skin.points.size() - influenced_point_count
              << " min_weight_sum=" << minimum_weight_sum
              << " max_weight_sum=" << maximum_weight_sum
              << " max_influences=" << maximum_influence_count << '\n';
    for (const bool hierarchical : {false, true}) {
        for (const bool inverse_rotation : {false, true}) {
            for (const bool reverse_multiply : {false, true}) {
                report_bind_reconstruction(
                    skin,
                    inverse_rotation,
                    reverse_multiply,
                    hierarchical);
            }
        }
    }
    if (argc == 6) {
        std::int32_t animation_reference{};
        std::size_t sequence_index{};
        try {
            animation_reference = std::stoi(argv[4]);
            sequence_index =
                static_cast<std::size_t>(std::stoull(argv[5]));
        } catch (const std::exception&) {
            std::cerr << "animation reference and sequence index must be "
                         "non-negative decimal integers\n";
            return EXIT_FAILURE;
        }
        const auto animation = hpvr::wand::load_hp1_animation(
            std::filesystem::path(argv[3]), animation_reference);
        if (animation.status != hpvr::wand::Hp1ProfileStatus::ok ||
            sequence_index >= animation.moves.size()) {
            std::cerr << "animation_sample_status="
                      << static_cast<int>(animation.status)
                      << " error=" << animation.error << '\n';
            return EXIT_FAILURE;
        }
        const auto first = hpvr::wand::sample_hp1_skeletal_animation(
            skin, animation, sequence_index, 0.0F);
        const auto middle = hpvr::wand::sample_hp1_skeletal_animation(
            skin,
            animation,
            sequence_index,
            animation.moves[sequence_index].track_time * 0.5F);
        if (first.status != hpvr::wand::Hp1ProfileStatus::ok ||
            middle.status != hpvr::wand::Hp1ProfileStatus::ok ||
            first.points.size() != middle.points.size()) {
            std::cerr << "animation_sample_status="
                      << static_cast<int>(first.status)
                      << " first_error=" << first.error
                      << " middle_error=" << middle.error << '\n';
            return EXIT_FAILURE;
        }
        double squared_displacement = 0.0;
        float maximum_displacement = 0.0F;
        for (std::size_t point = 0; point < first.points.size(); ++point) {
            const float x = middle.points[point].x - first.points[point].x;
            const float y = middle.points[point].y - first.points[point].y;
            const float z = middle.points[point].z - first.points[point].z;
            const float displacement =
                std::sqrt(x * x + y * y + z * z);
            squared_displacement +=
                static_cast<double>(displacement) * displacement;
            maximum_displacement =
                std::max(maximum_displacement, displacement);
        }
        const float rms_displacement = std::sqrt(
            static_cast<float>(
                squared_displacement /
                static_cast<double>(first.points.size())));
        std::cout << "animation_sample_status=ok"
                  << " animation=" << animation.object_name
                  << " sequence_index=" << sequence_index
                  << " sequence=" << animation.sequences[sequence_index].name
                  << " sample0=" << first.sample_time_seconds
                  << " sample1=" << middle.sample_time_seconds
                  << " points=" << first.points.size()
                  << " rms_displacement=" << rms_displacement
                  << " max_displacement=" << maximum_displacement << '\n';
    }
    return EXIT_SUCCESS;
}
