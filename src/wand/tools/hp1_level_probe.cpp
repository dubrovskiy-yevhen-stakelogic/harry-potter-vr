#include "hpvr/hp1_gesture.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: hpvr_hp1_level_probe <map-package>\n";
        return EXIT_FAILURE;
    }
    const auto result = hpvr::wand::inspect_hp1_level_handles(
        std::filesystem::path(argv[1]));
    if (result.status != hpvr::wand::Hp1ProfileStatus::ok) {
        std::cerr << "level_status=" << static_cast<int>(result.status)
                  << " error=" << result.error << '\n';
        return EXIT_FAILURE;
    }
    const auto starts = hpvr::wand::inspect_hp1_player_starts(
        std::filesystem::path(argv[1]));
    if (starts.status != hpvr::wand::Hp1ProfileStatus::ok) {
        std::cerr << "player_start_status=" << static_cast<int>(starts.status)
                  << " error=" << starts.error << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "level_status=ok"
              << " package_version=" << result.package_version
              << " level_reference=" << result.level_reference
              << " world_model_reference=" << result.world_model_reference
              << " actor_slots=" << result.actor_references.size()
              << " actor_refs="
              << result.actor_references.size() - result.null_actor_count
              << " null_slots=" << result.null_actor_count
              << " player_starts=" << starts.player_starts.size() << '\n';
    for (std::size_t ordinal = 0; ordinal < starts.player_starts.size();
         ++ordinal) {
        const auto& start = starts.player_starts[ordinal];
        std::cout << "player_start=" << ordinal
                  << " actor_ref=" << start.actor_reference
                  << " actor_slot=" << start.actor_slot_index
                  << " object=" << start.object_name
                  << " location_serialized=" << start.location_serialized
                  << " location_unreal=" << start.location_unreal.x << ','
                  << start.location_unreal.y << ','
                  << start.location_unreal.z
                  << " rotation_serialized=" << start.rotation_serialized
                  << " rotation_units=" << start.rotation_units[0] << ','
                  << start.rotation_units[1] << ','
                  << start.rotation_units[2] << '\n';
    }
    return EXIT_SUCCESS;
}
