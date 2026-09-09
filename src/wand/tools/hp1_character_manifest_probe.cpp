#include "hpvr/hp1_package_linker.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        std::cerr << "usage: hpvr_hp1_character_manifest_probe <data-root> "
                     "<map-package> [excluded-actor-reference]\n";
        return EXIT_FAILURE;
    }
    std::int32_t excluded = 0;
    if (argc == 4) {
        std::size_t consumed = 0;
        const auto parsed = std::stoll(argv[3], &consumed, 10);
        if (consumed != std::string(argv[3]).size() || parsed < 0 ||
            parsed > 0x7fffffffLL) {
            std::cerr << "invalid excluded actor reference\n";
            return EXIT_FAILURE;
        }
        excluded = static_cast<std::int32_t>(parsed);
    }
    const auto result = hpvr::wand::build_hp1_character_manifest(
        std::filesystem::path(argv[1]),
        std::filesystem::path(argv[2]), excluded);
    if (result.status != hpvr::wand::Hp1ProfileStatus::ok) {
        std::cerr << "character_manifest_status="
                  << static_cast<int>(result.status)
                  << " error=" << result.error << '\n';
        return EXIT_FAILURE;
    }
    for (const auto& actor : result.actors) {
        std::cout << "actor_ref=" << actor.actor_reference
                  << " class_ref=" << actor.class_reference
                  << " slot=" << actor.actor_slot_index
                  << " class=" << actor.qualified_class_name
                  << " object=" << actor.object_name
                  << " location_unreal=" << actor.location_unreal.x << ','
                  << actor.location_unreal.y << ','
                  << actor.location_unreal.z
                  << " rotation_units=" << actor.rotation_units[0] << ','
                  << actor.rotation_units[1] << ','
                  << actor.rotation_units[2]
                  << " draw_scale=" << actor.draw_scale
                  << " mesh_package=" << actor.mesh_package_name
                  << " mesh_ref=" << actor.mesh_reference
                  << " mesh=" << actor.mesh_object_path << '\n';
    }
    std::cout << "character_manifest_status=ok"
              << " inspected=" << result.inspected_actor_count
              << " selected=" << result.actors.size()
              << " excluded=" << result.excluded_actor_count
              << " non_character=" << result.non_character_actor_count
              << " missing_location=" << result.missing_location_count
              << " unresolved_class=" << result.unresolved_class_count
              << " missing_mesh=" << result.missing_mesh_count
              << " hidden=" << result.hidden_actor_count << '\n';
    return EXIT_SUCCESS;
}
