#include "hpvr/hp1_gesture.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: hpvr_hp1_bsp_mesh_probe <map-package>\n";
        return EXIT_FAILURE;
    }
    const auto topology = hpvr::wand::load_hp1_bsp_topology(
        std::filesystem::path(argv[1]));
    if (topology.status != hpvr::wand::Hp1ProfileStatus::ok) {
        std::cerr << "mesh_status=" << static_cast<int>(topology.status)
                  << " stage=topology error=" << topology.error << '\n';
        return EXIT_FAILURE;
    }
    // Unit scale validates geometry and axes without claiming a calibrated
    // Harry Potter world-to-meter ratio.
    const auto mesh = hpvr::wand::build_hp1_bsp_triangle_mesh(topology, 1.0F);
    if (mesh.status != hpvr::wand::Hp1ProfileStatus::ok) {
        std::cerr << "mesh_status=" << static_cast<int>(mesh.status)
                  << " stage=triangles error=" << mesh.error << '\n';
        return EXIT_FAILURE;
    }
    std::size_t static_triangles = 0;
    std::size_t actor_bound_triangles = 0;
    std::size_t textured_triangles = 0;
    std::size_t unreferenced_texture_triangles = 0;
    std::size_t imported_texture_triangles = 0;
    std::size_t local_texture_triangles = 0;
    std::size_t imported_actor_triangles = 0;
    std::size_t local_actor_triangles = 0;
    std::set<std::uint32_t> polygon_flag_values;
    for (const auto& triangle : mesh.triangles) {
        const auto& surface = topology.surfaces[triangle.surface_index];
        if (surface.actor_reference == 0) {
            ++static_triangles;
        } else {
            ++actor_bound_triangles;
        }
        if (surface.texture_reference == 0) {
            ++unreferenced_texture_triangles;
        } else {
            ++textured_triangles;
            if (surface.texture_reference < 0) {
                ++imported_texture_triangles;
            } else {
                ++local_texture_triangles;
            }
        }
        if (surface.actor_reference < 0) {
            ++imported_actor_triangles;
        } else if (surface.actor_reference > 0) {
            ++local_actor_triangles;
        }
        polygon_flag_values.insert(surface.polygon_flags);
    }
    std::cout << "mesh_status=ok"
              << " polygons=" << mesh.source_polygon_count
              << " short_polygons=" << mesh.short_polygon_count
              << " triangles=" << mesh.triangles.size()
              << " degenerates=" << mesh.degenerate_triangle_count
              << " winding_reversals=" << mesh.reversed_winding_count
              << " static_triangles=" << static_triangles
              << " actor_bound_triangles=" << actor_bound_triangles
              << " textured_triangles=" << textured_triangles
              << " no_texture_triangles=" << unreferenced_texture_triangles
              << " imported_texture_triangles=" << imported_texture_triangles
              << " local_texture_triangles=" << local_texture_triangles
              << " imported_actor_triangles=" << imported_actor_triangles
              << " local_actor_triangles=" << local_actor_triangles
              << " polygon_flag_values=" << polygon_flag_values.size()
              << " validation_scale=1" << '\n';
    return EXIT_SUCCESS;
}
