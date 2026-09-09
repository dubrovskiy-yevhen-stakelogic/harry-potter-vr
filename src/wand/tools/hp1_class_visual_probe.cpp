#include "hpvr/hp1_gesture.h"

#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

[[nodiscard]] std::string display_path(
    const std::vector<std::string>& components) {
    std::string result;
    for (const auto& component : components) {
        if (!result.empty()) {
            result.push_back('.');
        }
        result += component;
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: hpvr_hp1_class_visual_probe <package> "
                     "<local-class-reference>\n";
        return EXIT_FAILURE;
    }
    std::size_t consumed = 0;
    const auto parsed = std::stoll(argv[2], &consumed, 10);
    if (consumed != std::string(argv[2]).size() || parsed <= 0 ||
        parsed > 0x7fffffffLL) {
        std::cerr << "invalid local class reference\n";
        return EXIT_FAILURE;
    }
    const auto result = hpvr::wand::inspect_hp1_class_visual_defaults(
        std::filesystem::path(argv[1]), static_cast<std::int32_t>(parsed));
    if (result.status != hpvr::wand::Hp1ProfileStatus::ok) {
        std::cerr << "class_visual_status="
                  << static_cast<int>(result.status)
                  << " error=" << result.error << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "class_visual_status=ok"
              << " version=" << result.package_version
              << " class_ref=" << result.class_reference
              << " class=" << result.object_name
              << " super_ref=" << result.super_reference
              << " super_path=" << display_path(result.super_object_path)
              << " properties=" << result.property_count
              << " draw_scale=" << result.draw_scale
              << " draw_scale_serialized="
              << result.draw_scale_serialized
              << " mesh_ref=" << result.mesh_reference
              << " mesh_path=" << display_path(result.mesh_object_path)
              << " mesh_serialized=" << result.mesh_serialized
              << " skins=" << result.skin_references.size()
              << " anim="
               << (result.animation_sequence_serialized
                       ? result.animation_sequence
                       : "<inherited>")
               << " base_eye_height=" << result.base_eye_height
               << " base_eye_height_serialized="
               << result.base_eye_height_serialized
               << " collision_height=" << result.collision_height
               << " collision_height_serialized="
               << result.collision_height_serialized
               << " draw_type=" << static_cast<unsigned>(result.draw_type)
              << " draw_type_serialized=" << result.draw_type_serialized
              << " hidden=" << result.hidden
              << " hidden_serialized=" << result.hidden_serialized << '\n';
    for (const auto& property : result.serialized_properties) {
        std::cout << "property=" << property.name
                  << " kind=" << static_cast<unsigned>(property.kind)
                  << " structure="
                  << (property.structure_name.empty()
                          ? "<none>"
                          : property.structure_name)
                  << " array_index=" << property.array_index
                  << " boolean="
                  << (property.boolean_value_serialized
                          ? (property.boolean_value ? "true" : "false")
                          : "<none>")
                  << " object_ref="
                  << (property.object_reference_serialized
                          ? std::to_string(property.object_reference)
                          : "<none>")
                  << " object_path="
                  << (property.object_reference_serialized
                          ? display_path(property.object_path)
                          : "<none>")
                  << " value_hex=";
        for (const auto byte : property.value) {
            std::cout << std::hex << std::setw(2) << std::setfill('0')
                      << static_cast<unsigned>(byte);
        }
        std::cout << std::dec << '\n';
    }
    return EXIT_SUCCESS;
}
