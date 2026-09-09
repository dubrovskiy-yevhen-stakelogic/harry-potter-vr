#include "hpvr/hp1_gesture.h"
#include "hpvr/hp1_package_linker.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
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

struct Usage {
    std::size_t surface_count{};
    std::size_t triangle_count{};
};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr
            << "usage: hpvr_hp1_bsp_material_probe <data-root> <map-package>\n";
        return EXIT_FAILURE;
    }

    const std::filesystem::path data_root(argv[1]);
    const std::filesystem::path map_package(argv[2]);
    const auto topology = hpvr::wand::load_hp1_bsp_topology(map_package);
    if (topology.status != hpvr::wand::Hp1ProfileStatus::ok) {
        std::cerr << "topology_status=" << static_cast<int>(topology.status)
                  << " error=" << topology.error << '\n';
        return EXIT_FAILURE;
    }
    const auto linked =
        hpvr::wand::link_hp1_package_graph(data_root, map_package);
    if (linked.status != hpvr::wand::Hp1PackageLinkStatus::ok) {
        std::cerr << "link_status=" << static_cast<int>(linked.status)
                  << " error=" << linked.error << '\n';
        return EXIT_FAILURE;
    }

    const auto source_name = ascii_fold(map_package.stem().string());
    std::map<std::int32_t, const hpvr::wand::Hp1ResolvedImport*> imports;
    for (const auto& import : linked.imports) {
        if (ascii_fold(import.source_package) == source_name) {
            if (!imports.emplace(import.source_reference, &import).second) {
                std::cerr << "duplicate_source_reference="
                          << import.source_reference << '\n';
                return EXIT_FAILURE;
            }
        }
    }

    std::map<std::int32_t, Usage> usage;
    for (const auto& surface : topology.surfaces) {
        ++usage[surface.texture_reference].surface_count;
    }
    for (const auto& node : topology.nodes) {
        if (node.vertex_count >= 3) {
            const auto surface_index =
                static_cast<std::size_t>(node.surface_index);
            usage[topology.surfaces[surface_index].texture_reference]
                .triangle_count +=
                static_cast<std::size_t>(node.vertex_count) - 2;
        }
    }

    std::size_t resolved_texture_count = 0;
    std::size_t unresolved_count = 0;
    std::size_t non_base_texture_class_count = 0;
    std::size_t invalid_target_count = 0;
    std::map<std::string, std::filesystem::path> package_paths;
    for (const auto& package : linked.graph.packages) {
        if (package.kind == hpvr::wand::Hp1ResolvedPackageKind::data_package) {
            package_paths.emplace(ascii_fold(package.package_name), package.path);
        }
    }
    std::cout << "material_status=ok"
              << " map=" << map_package.string()
              << " surfaces=" << topology.surfaces.size()
              << " unique_texture_refs=" << usage.size() << '\n';
    for (const auto& [reference, counts] : usage) {
        const auto found = imports.find(reference);
        if (reference >= 0 || found == imports.end()) {
            ++unresolved_count;
            std::cout << "texture_ref=" << reference
                      << " surfaces=" << counts.surface_count
                      << " fan_triangles=" << counts.triangle_count
                      << " resolution=UNRESOLVED\n";
            continue;
        }
        const auto& target = *found->second;
        const bool resolved_export =
            target.target_kind ==
                hpvr::wand::Hp1ImportTargetKind::export_object &&
            target.target_reference > 0;
        const bool exact_texture =
            resolved_export &&
            ascii_fold(target.qualified_class_name) == "engine.texture";
        if (exact_texture) {
            ++resolved_texture_count;
        } else if (resolved_export) {
            ++non_base_texture_class_count;
        } else {
            ++invalid_target_count;
        }
        const auto package_path = package_paths.find(
            ascii_fold(target.target_package));
        std::cout << "texture_ref=" << reference
                  << " surfaces=" << counts.surface_count
                  << " fan_triangles=" << counts.triangle_count
                  << " class=" << target.qualified_class_name
                  << " target_package=" << target.target_package
                  << " target_file="
                  << (package_path == package_paths.end()
                          ? std::string("<none>")
                          : package_path->second.string())
                  << " target_ref=" << target.target_reference
                  << " target_path=" << display_path(target.target_object_path)
                  << " resolution="
                  << (exact_texture
                          ? "EXACT_TEXTURE"
                          : (resolved_export ? "NON_BASE_TEXTURE_CLASS"
                                             : "WRONG_KIND"))
                  << '\n';
    }
    std::cout << "material_result=ok"
              << " exact_textures=" << resolved_texture_count
              << " unresolved=" << unresolved_count
              << " non_base_texture_classes="
              << non_base_texture_class_count
              << " invalid_targets=" << invalid_target_count << '\n';
    return unresolved_count == 0 && invalid_target_count == 0
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
