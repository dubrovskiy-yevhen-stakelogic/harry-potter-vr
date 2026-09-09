#include "hpvr/hp1_gesture.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <cstdint>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: hpvr_hp1_bsp_probe <map-package>\n";
        return EXIT_FAILURE;
    }
    const auto result = hpvr::wand::load_hp1_bsp_topology(
        std::filesystem::path(argv[1]));
    if (result.status != hpvr::wand::Hp1ProfileStatus::ok) {
        std::cerr << "bsp_status=" << static_cast<int>(result.status)
                  << " error=" << result.error << '\n';
        return EXIT_FAILURE;
    }
    std::int32_t maximum_u = 0;
    std::int32_t maximum_v = 0;
    std::uint64_t total_texels = 0;
    std::size_t maps_with_lights = 0;
    std::size_t maximum_lights = 0;
    for (const auto& light_map : result.light_maps) {
        maximum_u = std::max(maximum_u, light_map.u_clamp);
        maximum_v = std::max(maximum_v, light_map.v_clamp);
        total_texels += static_cast<std::uint64_t>(light_map.u_clamp) *
                        static_cast<std::uint64_t>(light_map.v_clamp);
        std::size_t count = 0;
        if (light_map.light_actor_index >= 0) {
            for (std::size_t index = static_cast<std::size_t>(
                     light_map.light_actor_index);
                 index < result.light_references.size() &&
                 result.light_references[index] != 0; ++index) {
                ++count;
            }
        }
        maps_with_lights += count != 0 ? 1U : 0U;
        maximum_lights = std::max(maximum_lights, count);
    }
    std::cout << "bsp_status=ok"
              << " package_version=" << result.package_version
              << " model_reference=" << result.model_reference
              << " vectors=" << result.vectors.size()
              << " points=" << result.points.size()
              << " nodes=" << result.nodes.size()
              << " surfaces=" << result.surfaces.size()
              << " vertices=" << result.vertices.size()
              << " shared_sides=" << result.shared_side_count
              << " zones=" << result.zone_count
              << " light_maps=" << result.light_maps.size()
              << " light_bits=" << result.light_bits.size()
              << " light_refs=" << result.light_references.size()
              << " lightmap_max=" << maximum_u << 'x' << maximum_v
              << " lightmap_texels=" << total_texels
              << " lit_maps=" << maps_with_lights
              << " max_lights_per_map=" << maximum_lights
              << " bounds=" << result.bound_count
              << " leaves=" << result.leaf_count << '\n';
    return EXIT_SUCCESS;
}
