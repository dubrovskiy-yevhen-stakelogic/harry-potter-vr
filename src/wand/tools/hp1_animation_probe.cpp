#include "hpvr/hp1_gesture.h"

#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: hpvr_hp1_animation_probe <animation-package> "
                     "<export-reference> [export-reference ...]\n";
        return EXIT_FAILURE;
    }
    bool all_ok = true;
    for (int argument = 2; argument < argc; ++argument) {
        std::size_t consumed = 0;
        const std::string text(argv[argument]);
        long long parsed = 0;
        try {
            parsed = std::stoll(text, &consumed, 10);
        } catch (const std::exception&) {
            consumed = 0;
        }
        if (consumed != text.size() || parsed <= 0 ||
            parsed > 0x7fffffffLL) {
            std::cerr << "invalid animation export reference=" << text << '\n';
            all_ok = false;
            continue;
        }
        const auto result = hpvr::wand::load_hp1_animation(
            std::filesystem::path(argv[1]),
            static_cast<std::int32_t>(parsed));
        if (result.status != hpvr::wand::Hp1ProfileStatus::ok) {
            std::cerr << "animation_status="
                      << static_cast<int>(result.status)
                      << " version=" << result.package_version
                      << " animation_ref=" << result.animation_reference
                      << " object=" << result.object_name
                      << " parsed_bones=" << result.bones.size()
                      << " parsed_moves=" << result.moves.size()
                      << " error=" << result.error << '\n';
            all_ok = false;
            continue;
        }

        std::size_t track_count = 0;
        float maximum_quaternion_length_error = 0.0F;
        float maximum_position_magnitude = 0.0F;
        float maximum_key_time = 0.0F;
        float minimum_time_sum_ratio =
            std::numeric_limits<float>::infinity();
        float maximum_time_sum_ratio = 0.0F;
        std::size_t dynamic_position_track_count = 0;
        std::size_t mismatched_dynamic_position_time_count = 0;
        std::size_t non_identity_bone_mappings = 0;
        for (const auto& key : result.compressed_orientation_keys) {
            const auto value =
                hpvr::wand::decode_hp1_animation_orientation_key(key);
            const float length_squared =
                value.x * value.x + value.y * value.y +
                value.z * value.z + value.w * value.w;
            maximum_quaternion_length_error = std::max(
                maximum_quaternion_length_error,
                std::abs(length_squared - 1.0F));
        }
        for (const auto& move : result.moves) {
            track_count += move.tracks.size();
            for (std::size_t bone = 0;
                 bone < move.bone_indices.size();
                 ++bone) {
                if (move.bone_indices[bone] !=
                    static_cast<std::int32_t>(bone)) {
                    ++non_identity_bone_mappings;
                }
            }
            for (const auto& track : move.tracks) {
                for (std::size_t index = 0;
                     index < track.position_count;
                     ++index) {
                    const auto value =
                        hpvr::wand::decode_hp1_animation_position_key(
                            result.compressed_position_keys[
                                track.position_offset + index],
                            track.position_scale);
                    maximum_position_magnitude = std::max(
                        maximum_position_magnitude,
                        std::sqrt(value.x * value.x + value.y * value.y +
                                  value.z * value.z));
                }
                for (std::size_t index = 0;
                     index < track.time_count;
                     ++index) {
                    const float key_time =
                        hpvr::wand::decode_hp1_animation_time_key(
                            result.compressed_time_keys[
                                track.time_offset + index],
                            track.time_scale);
                    maximum_key_time =
                        std::max(maximum_key_time, key_time);
                }
                float time_sum = 0.0F;
                for (std::size_t index = 0;
                     index < track.time_count;
                     ++index) {
                    time_sum += hpvr::wand::decode_hp1_animation_time_key(
                        result.compressed_time_keys[
                            track.time_offset + index],
                        track.time_scale);
                }
                if (move.track_time > 0.0F && track.time_count > 1) {
                    const float ratio = time_sum / move.track_time;
                    minimum_time_sum_ratio =
                        std::min(minimum_time_sum_ratio, ratio);
                    maximum_time_sum_ratio =
                        std::max(maximum_time_sum_ratio, ratio);
                }
                if (track.position_count > 1) {
                    ++dynamic_position_track_count;
                    if (track.position_count != track.time_count) {
                        ++mismatched_dynamic_position_time_count;
                    }
                }
            }
        }
        if (!std::isfinite(minimum_time_sum_ratio)) {
            minimum_time_sum_ratio = 0.0F;
        }
        std::cout << std::setprecision(8)
                  << "animation_status=ok"
                  << " version=" << result.package_version
                  << " animation_ref=" << result.animation_reference
                  << " object=" << result.object_name
                  << " bones=" << result.bones.size()
                  << " moves=" << result.moves.size()
                  << " tracks=" << track_count
                  << " orientation_keys=" << result.orientation_key_count
                  << " position_keys=" << result.position_key_count
                  << " time_keys=" << result.time_key_count
                  << " sequences=" << result.sequences.size()
                  << " max_quat_len2_error="
                  << maximum_quaternion_length_error
                  << " max_position=" << maximum_position_magnitude
                  << " max_key_time=" << maximum_key_time
                  << " min_time_sum_ratio=" << minimum_time_sum_ratio
                  << " max_time_sum_ratio=" << maximum_time_sum_ratio
                  << " dynamic_position_tracks="
                  << dynamic_position_track_count
                  << " mismatched_dynamic_position_times="
                  << mismatched_dynamic_position_time_count
                  << " non_identity_bone_mappings="
                  << non_identity_bone_mappings << '\n';
        for (std::size_t index = 0; index < result.sequences.size(); ++index) {
            const auto& sequence = result.sequences[index];
            std::cout << "sequence=" << index
                      << " name=" << sequence.name
                      << " group=" << sequence.group
                      << " start=" << sequence.start_frame
                      << " frames=" << sequence.frame_count
                      << " rate=" << sequence.rate
                      << " notifies=" << sequence.notify_count;
            if (index < result.moves.size()) {
                const auto& move = result.moves[index];
                std::cout << " track_time=" << move.track_time
                          << " start_bone=" << move.start_bone
                          << " move_flags=" << move.flags
                          << " bone_indices=" << move.bone_indices.size()
                          << " tracks=" << move.tracks.size();
                if (!move.tracks.empty() &&
                    move.tracks.front().time_count > 0) {
                    const auto& track = move.tracks.front();
                    std::cout << " track0_time_bytes=";
                    const auto displayed =
                        std::min<std::size_t>(track.time_count, 5);
                    for (std::size_t key = 0; key < displayed; ++key) {
                        std::cout
                            << static_cast<unsigned>(
                                   result.compressed_time_keys[
                                       track.time_offset + key])
                            << ',';
                    }
                    std::cout << "last="
                              << static_cast<unsigned>(
                                     result.compressed_time_keys[
                                         track.time_offset +
                                         track.time_count - 1]);
                }
            }
            std::cout << '\n';
        }
    }
    return all_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
