#include "hpvr/hp1_gesture.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    if (argc == 3) {
        const auto topology = hpvr::wand::load_hp1_brush_topology(argv[1], std::atoi(argv[2]));
        const auto mesh = hpvr::wand::build_hp1_bsp_triangle_mesh(topology, 0.01F);
        std::cout << "brush=" << argv[2] << " status=" << static_cast<int>(topology.status)
                  << " nodes=" << topology.nodes.size() << " surfaces=" << topology.surfaces.size()
                  << " triangles=" << mesh.triangles.size() << " error=" << topology.error << ' ' << mesh.error << '\n';
        for(const auto& surface:topology.surfaces)
            std::cout<<"texture="<<surface.texture_reference<<" flags="<<surface.polygon_flags<<'\n';
        return mesh.status == hpvr::wand::Hp1ProfileStatus::ok ? 0 : 1;
    }
    if (argc != 2) {
        std::cerr << "usage: hpvr_hp1_model_probe <map-package>\n";
        return EXIT_FAILURE;
    }
    const auto result = hpvr::wand::inspect_hp1_model_census(
        std::filesystem::path(argv[1]));
    if (result.status != hpvr::wand::Hp1ProfileStatus::ok) {
        std::cerr << "model_status=" << static_cast<int>(result.status)
                  << " error=" << result.error << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "model_status=ok"
              << " package_version=" << result.package_version
              << " model_reference=" << result.model_reference
              << " polys_reference=" << result.polys_reference
              << " vectors=" << result.vector_count
              << " points=" << result.point_count
              << " nodes=" << result.node_count
              << " surfaces=" << result.surface_count
              << " vertices=" << result.vertex_count
              << " shared_sides=" << result.shared_side_count
              << " zones=" << result.zone_count
              << " light_maps=" << result.light_map_count
              << " light_bit_bytes=" << result.light_bit_bytes
              << " bounds=" << result.bound_count
              << " leaf_hulls=" << result.leaf_hull_count
              << " leaves=" << result.leaf_count
              << " lights=" << result.light_count
              << " null_lights=" << result.null_light_count
              << " root_outside=" << (result.root_outside ? 1 : 0)
              << " linked=" << (result.linked ? 1 : 0) << '\n';
    return EXIT_SUCCESS;
}
