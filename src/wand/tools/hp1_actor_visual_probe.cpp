#include "hpvr/hp1_gesture.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] std::string ascii_fold(std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

[[nodiscard]] std::string bytes_hex(
    const std::vector<std::uint8_t>& value) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (const auto byte : value) {
        stream << std::setw(2) << static_cast<unsigned>(byte);
    }
    return stream.str();
}

[[nodiscard]] std::string join_path(
    const std::vector<std::string>& components) {
    std::string result;
    for (const auto& component : components) {
        if (!result.empty()) result += '.';
        result += component;
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 4 ||
        (argc == 4 && std::string_view(argv[3]) != "--properties")) {
        std::cerr << "usage: hpvr_hp1_actor_visual_probe <map-package> "
                     "[class-or-object-substring] [--properties]\n";
        return EXIT_FAILURE;
    }
    const auto result = hpvr::wand::inspect_hp1_actor_visuals(
        std::filesystem::path(argv[1]));
    if (result.status != hpvr::wand::Hp1ProfileStatus::ok) {
        std::cerr << "actor_visual_status="
                  << static_cast<int>(result.status)
                  << " error=" << result.error << '\n';
        return EXIT_FAILURE;
    }
    const auto filter = argc >= 3 ? ascii_fold(argv[2]) : std::string{};
    const bool print_properties = argc == 4;
    std::size_t matched = 0;
    std::size_t located = 0;
    std::size_t mesh_overrides = 0;
    for (const auto& actor : result.actors) {
        if (!filter.empty() &&
            ascii_fold(actor.qualified_class_name).find(filter) ==
                std::string::npos &&
            ascii_fold(actor.object_name).find(filter) == std::string::npos) {
            continue;
        }
        ++matched;
        located += actor.location_serialized ? 1U : 0U;
        mesh_overrides += actor.mesh_serialized ? 1U : 0U;
        std::cout << "actor_ref=" << actor.actor_reference
                  << " class_ref=" << actor.class_reference
                  << " slot=" << actor.actor_slot_index
                  << " class=" << actor.qualified_class_name
                  << " object=" << actor.object_name
                  << " properties=" << actor.property_count
                  << " location_serialized=" << actor.location_serialized
                  << " location_unreal=" << actor.location_unreal.x << ','
                  << actor.location_unreal.y << ','
                  << actor.location_unreal.z
                  << " rotation_serialized=" << actor.rotation_serialized
                  << " rotation_units=" << actor.rotation_units[0] << ','
                  << actor.rotation_units[1] << ','
                  << actor.rotation_units[2]
                  << " draw_scale=" << actor.draw_scale
                  << " draw_scale_serialized="
                  << actor.draw_scale_serialized
                  << " mesh_ref=" << actor.mesh_reference
                  << " mesh_serialized=" << actor.mesh_serialized
                  << " skins=" << actor.skin_references.size()
                  << " anim="
                  << (actor.animation_sequence_serialized
                          ? actor.animation_sequence
                          : "<default>")
                  << " draw_type=" << static_cast<unsigned>(actor.draw_type)
                  << " draw_type_serialized=" << actor.draw_type_serialized
                  << " hidden=" << actor.hidden
                  << " hidden_serialized=" << actor.hidden_serialized
                  << " tag=" << (actor.tag_serialized ? actor.tag : "<default>")
                  << " event=" << (actor.event_serialized ? actor.event : "<none>")
                  << " initial_state="
                  << (actor.initial_state_serialized ? actor.initial_state : "<default>")
                  << " ambient_sound_ref=" << actor.ambient_sound_reference
                  << " sound_volume=" << static_cast<unsigned>(actor.sound_volume)
                  << " sound_radius=" << static_cast<unsigned>(actor.sound_radius)
                  << " collision_radius=" << actor.collision_radius
                  << " collision_height=" << actor.collision_height
                  << " block_players=" << actor.block_players
                  << " light=" << static_cast<unsigned>(actor.light_brightness)
                  << ',' << static_cast<unsigned>(actor.light_hue)
                  << ',' << static_cast<unsigned>(actor.light_saturation)
                  << ',' << static_cast<unsigned>(actor.light_radius)
                  << '\n';
        if (print_properties) {
            for (const auto& property : actor.serialized_properties) {
                std::cout << "  property=" << property.name
                          << " kind=" << static_cast<unsigned>(property.kind)
                          << " struct="
                          << (property.structure_name.empty()
                                  ? "<none>" : property.structure_name)
                          << " array=" << property.array_index;
                if (property.boolean_value_serialized) {
                    std::cout << " bool=" << property.boolean_value;
                }
                if (property.text_value_serialized) {
                    std::cout << " text=" << std::quoted(property.text_value);
                }
                if (property.object_reference_serialized) {
                    std::cout << " object_ref=" << property.object_reference
                              << " object_path="
                              << join_path(property.object_path);
                }
                std::cout << " bytes=" << bytes_hex(property.value) << '\n';
            }
        }
    }
    std::cout << "actor_visual_result=ok"
              << " total_actors=" << result.actors.size()
              << " matched=" << matched
              << " located=" << located
              << " mesh_overrides=" << mesh_overrides
              << " filter=" << (filter.empty() ? "<none>" : filter)
              << '\n';
    return EXIT_SUCCESS;
}
